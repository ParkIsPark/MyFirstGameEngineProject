#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <vector>

#include "UMeshRayTracer.h"
#include "UMesh.h"
#include "ACamera.h"
#include "BVH.h"

// Triangles are passed to the shader through a *texture buffer object* (TBO,
// core since GL 3.1 / GLSL 1.40), NOT an SSBO. SSBOs need GL 4.3, which we must
// not assume on the target (grading) machine -- the rest of the engine's GPU
// shaders already target #version 330, so 3.3 is our baseline. Each triangle is
// 7 RGBA32F texels: v0,v1,v2,n0,n1,n2,albedo (w unused). RGBA32F is a mandatory
// texture-buffer format in 3.1+, unlike RGB32F.
static const int TEXELS_PER_TRI = 7;

static const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* FRAG_SRC = R"GLSL(
#version 330 core
out vec4 FragColor;

uniform vec3  uEye, uU, uV, uW;
uniform float uL, uR, uB, uT, uD;
uniform int   uWidth, uHeight, uNumTris;
uniform vec3  uLightPos, uLightColor;
uniform vec3  uKs;                    // specular coefficient (per-tri albedo = diffuse)
uniform float uShininess;             // Phong exponent
uniform samplerBuffer uTris;          // 7 texels per triangle (see C++ side)
uniform samplerBuffer uNodes;         // BVH: 2 texels per node (bbMin|left, bbMax|count)
uniform samplerBuffer uTriIdx;        // BVH leaf -> triangle index (R32F)
uniform int uNumNodes;

vec3 triTexel(int tri, int slot) { return texelFetch(uTris, tri * 7 + slot).xyz; }

bool rayTri(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2,
            out float t, out float u, out float v) {
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 p  = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-8) return false;
    float inv = 1.0 / det;
    vec3 s = ro - v0;
    u = dot(s, p) * inv;            if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    v = dot(rd, q) * inv;           if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;           return t > 1e-4;
}

vec3 skyColor(vec3 rd) {
    float k = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.10, 0.12, 0.16), vec3(0.40, 0.55, 0.80), k);
}

// Ray vs AABB slab (camera/shadow rays are ~never axis-parallel through a face).
bool slab(vec3 ro, vec3 invD, vec3 mn, vec3 mx, float tMax) {
    vec3 t0 = (mn - ro) * invD;
    vec3 t1 = (mx - ro) * invD;
    vec3 te = min(t0, t1), tx = max(t0, t1);
    float enter = max(max(te.x, te.y), max(te.z, 0.0));
    float exit  = min(min(tx.x, tx.y), min(tx.z, tMax));
    return enter <= exit;
}

// Stack-based BVH closest-hit traversal (1:1 port of the CPU BVH).
bool traceClosest(vec3 ro, vec3 rd, out float t, out int tri, out float u, out float v) {
    vec3 invD = 1.0 / rd;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    t = 1e30; tri = -1;
    while (sp > 0) {
        int ni = stack[--sp];
        vec4 a = texelFetch(uNodes, ni * 2 + 0);
        vec4 b = texelFetch(uNodes, ni * 2 + 1);
        if (!slab(ro, invD, a.xyz, b.xyz, t)) continue;
        int rc = int(b.w);
        if (rc > 0) {                                   // leaf
            int start = int(a.w);
            for (int i = 0; i < rc; ++i) {
                int ti = int(texelFetch(uTriIdx, start + i).x);
                float tt, uu, vv;
                if (rayTri(ro, rd, triTexel(ti,0), triTexel(ti,1), triTexel(ti,2), tt, uu, vv) && tt < t) {
                    t = tt; tri = ti; u = uu; v = vv;
                }
            }
        } else if (sp + 2 <= 64) {                      // inner
            stack[sp++] = int(a.w);                      // left child
            stack[sp++] = -rc;                           // right child
        }
    }
    return tri >= 0;
}

// Reinhard tone map + gamma: compress accumulated light instead of clipping to
// flat white when ambient + diffuse + specular (and multiple lights) stack up.
vec3 tonemap(vec3 c) {
    c = c / (c + vec3(1.0));
    return pow(c, vec3(1.0 / 2.2));
}

void main() {
    float aspect = float(uWidth) / float(uHeight);
    float su = (uL + (uR - uL) * (gl_FragCoord.x / float(uWidth))) * aspect;
    float sv =  uB + (uT - uB) * (gl_FragCoord.y / float(uHeight));
    vec3 rd = normalize(-uD * uW + su * uU + sv * uV);   // ACamera convention
    vec3 ro = uEye;

    float closest; int hit; float hu, hv;
    if (!traceClosest(ro, rd, closest, hit, hu, hv)) {
        FragColor = vec4(tonemap(skyColor(rd)), 1.0); return;
    }

    vec3 n0 = triTexel(hit, 3);
    vec3 n1 = triTexel(hit, 4);
    vec3 n2 = triTexel(hit, 5);
    vec3 n  = normalize((1.0 - hu - hv) * n0 + hu * n1 + hv * n2);
    vec3 albedo = triTexel(hit, 6);                  // per-triangle diffuse
    vec3 hitPos = ro + closest * rd;

    // Blinn-Phong point light + Unreal-style sky ambient (hemisphere sky).
    vec3 L = normalize(uLightPos - hitPos);
    vec3 Vv = normalize(uEye - hitPos);
    vec3 H = normalize(L + Vv);
    float NdotL = max(dot(n, L), 0.0);
    float NdotH = max(dot(n, H), 0.0);

    vec3 ambient = albedo * skyColor(n) * 0.5;
    vec3 diffuse = albedo * NdotL;
    vec3 spec    = (NdotL > 0.0) ? uKs * pow(NdotH, max(uShininess, 1.0)) : vec3(0.0);
    vec3 col = ambient + (diffuse + spec) * uLightColor;
    FragColor = vec4(tonemap(col), 1.0);
}
)GLSL";

static GLuint compile(GLenum type, const char* src)
{
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[4096]; glGetShaderInfoLog(id, sizeof(buf), nullptr, buf);
               std::cerr << "UMeshRayTracer shader error:\n" << buf << "\n"; }
    return id;
}

void UMeshRayTracer::Init()
{
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    GLint ok; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[4096]; glGetProgramInfoLog(prog_, sizeof(buf), nullptr, buf);
               std::cerr << "UMeshRayTracer link error:\n" << buf << "\n"; }
    glDeleteShader(vs); glDeleteShader(fs);

    const float quad[] = { -1,-1,  1,-1,  -1,1,   -1,1,  1,-1,  1,1 };
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void UMeshRayTracer::UploadMesh(const UMesh& mesh, const glm::mat4& model)
{
    UploadWorld({ &mesh }, { model }, { mesh.material.kd }, lightPos_, lightColor_);
    mat_ = mesh.material;                      // restore ks/shininess for the demo path
}

void UMeshRayTracer::uploadTexels(const std::vector<glm::vec4>& texels)
{
    if (!tbo_) glGenBuffers(1, &tbo_);
    glBindBuffer(GL_TEXTURE_BUFFER, tbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(texels.size() * sizeof(glm::vec4)),
                 texels.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    if (!tex_) glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_BUFFER, tex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo_);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void UMeshRayTracer::uploadBVH(const BVH& bvh)
{
    // nodes: 2 RGBA32F texels (bbMin | float(left), bbMax | float(count)).
    std::vector<glm::vec4> nodeTexels;
    nodeTexels.reserve(bvh.nodes.size() * 2);
    for (const BVHNode& n : bvh.nodes)
    {
        nodeTexels.emplace_back(n.bbMin, static_cast<float>(n.leftOrTriStart));
        nodeTexels.emplace_back(n.bbMax, static_cast<float>(n.rightOrTriCount));
    }
    numNodes_ = static_cast<int>(bvh.nodes.size());
    if (!nodeTbo_) glGenBuffers(1, &nodeTbo_);
    glBindBuffer(GL_TEXTURE_BUFFER, nodeTbo_);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(nodeTexels.size() * sizeof(glm::vec4)),
                 nodeTexels.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    if (!nodeTex_) glGenTextures(1, &nodeTex_);
    glBindTexture(GL_TEXTURE_BUFFER, nodeTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, nodeTbo_);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    // leaf -> triangle index (R32F).
    std::vector<float> idx(bvh.triIndices.begin(), bvh.triIndices.end());
    if (!idxTbo_) glGenBuffers(1, &idxTbo_);
    glBindBuffer(GL_TEXTURE_BUFFER, idxTbo_);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(float)),
                 idx.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    if (!idxTex_) glGenTextures(1, &idxTex_);
    glBindTexture(GL_TEXTURE_BUFFER, idxTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, idxTbo_);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void UMeshRayTracer::UploadWorld(const std::vector<const UMesh*>& meshes,
                                 const std::vector<glm::mat4>& models,
                                 const std::vector<glm::vec3>& albedos,
                                 const glm::vec3& lightPos, const glm::vec3& lightColor)
{
    lightPos_ = lightPos; lightColor_ = lightColor;
    mat_.ks = glm::vec3(0.35f); mat_.shininess = 32.0f;

    // Combine all instances into one world-space mesh (+ per-triangle albedo),
    // then build a BVH over it for accelerated GPU traversal.
    UMesh combined;
    std::vector<glm::vec3> triAlbedo;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        if (!meshes[i]) continue;
        const UMesh& m = *meshes[i];
        const glm::mat3 nrmM = glm::inverseTranspose(glm::mat3(models[i]));
        const uint32_t base = static_cast<uint32_t>(combined.vertices.size());
        for (const Vertex& v : m.vertices)
        {
            Vertex w;
            w.position = glm::vec3(models[i] * glm::vec4(v.position, 1.0f));
            w.normal   = glm::normalize(nrmM * v.normal);
            w.uv       = v.uv;
            combined.vertices.push_back(w);
        }
        for (uint32_t idx : m.indices) combined.indices.push_back(base + idx);
        for (int t = 0; t < m.triangleCount(); ++t) triAlbedo.push_back(albedos[i]);
    }
    numTris_ = combined.triangleCount();
    combined.BuildBVH();

    // Bake 7 texels per triangle (combined order; BVH leaves index into this).
    std::vector<glm::vec4> texels;
    texels.reserve(static_cast<size_t>(numTris_) * TEXELS_PER_TRI);
    for (int t = 0; t < numTris_; ++t)
    {
        const Vertex& a = combined.vertices[combined.indices[3 * t + 0]];
        const Vertex& b = combined.vertices[combined.indices[3 * t + 1]];
        const Vertex& c = combined.vertices[combined.indices[3 * t + 2]];
        texels.emplace_back(a.position, 0.0f); texels.emplace_back(b.position, 0.0f); texels.emplace_back(c.position, 0.0f);
        texels.emplace_back(a.normal,   0.0f); texels.emplace_back(b.normal,   0.0f); texels.emplace_back(c.normal,   0.0f);
        texels.emplace_back(triAlbedo[t], 0.0f);
    }
    uploadTexels(texels);
    uploadBVH(*combined.bvh);
}

void UMeshRayTracer::RenderFrame(const ACamera& cam, int width, int height) const
{
    glUseProgram(prog_);
    glUniform3fv(glGetUniformLocation(prog_, "uEye"), 1, glm::value_ptr(cam.eye));
    glUniform3fv(glGetUniformLocation(prog_, "uU"),   1, glm::value_ptr(cam.u));
    glUniform3fv(glGetUniformLocation(prog_, "uV"),   1, glm::value_ptr(cam.v));
    glUniform3fv(glGetUniformLocation(prog_, "uW"),   1, glm::value_ptr(cam.w));
    glUniform1f (glGetUniformLocation(prog_, "uL"), cam.l);
    glUniform1f (glGetUniformLocation(prog_, "uR"), cam.r);
    glUniform1f (glGetUniformLocation(prog_, "uB"), cam.b);
    glUniform1f (glGetUniformLocation(prog_, "uT"), cam.t);
    glUniform1f (glGetUniformLocation(prog_, "uD"), cam.d);
    glUniform1i (glGetUniformLocation(prog_, "uWidth"),  width);
    glUniform1i (glGetUniformLocation(prog_, "uHeight"), height);
    glUniform1i (glGetUniformLocation(prog_, "uNumTris"), numTris_);
    glUniform1i (glGetUniformLocation(prog_, "uNumNodes"), numNodes_);
    glUniform3fv(glGetUniformLocation(prog_, "uLightPos"),   1, glm::value_ptr(lightPos_));
    glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, glm::value_ptr(lightColor_));
    glUniform3fv(glGetUniformLocation(prog_, "uKs"), 1, glm::value_ptr(mat_.ks));
    glUniform1f (glGetUniformLocation(prog_, "uShininess"), mat_.shininess);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, tex_);
    glUniform1i(glGetUniformLocation(prog_, "uTris"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER, nodeTex_);
    glUniform1i(glGetUniformLocation(prog_, "uNodes"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER, idxTex_);
    glUniform1i(glGetUniformLocation(prog_, "uTriIdx"), 2);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void UMeshRayTracer::Cleanup()
{
    if (tex_)     { glDeleteTextures(1, &tex_); tex_ = 0; }
    if (tbo_)     { glDeleteBuffers(1, &tbo_);  tbo_ = 0; }
    if (nodeTex_) { glDeleteTextures(1, &nodeTex_); nodeTex_ = 0; }
    if (nodeTbo_) { glDeleteBuffers(1, &nodeTbo_);  nodeTbo_ = 0; }
    if (idxTex_)  { glDeleteTextures(1, &idxTex_); idxTex_ = 0; }
    if (idxTbo_)  { glDeleteBuffers(1, &idxTbo_);  idxTbo_ = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);  vbo_  = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (prog_)    { glDeleteProgram(prog_); prog_ = 0; }
}
