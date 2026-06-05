#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <vector>
#include <algorithm>

#include "UMeshRayTracer.h"
#include "UMesh.h"
#include "ACamera.h"
#include "BVH.h"
#include "RTShading.h"     // shared lighting+shadow GLSL (same code as Hybrid)

#include <string>

// Triangles are passed to the shader through a *texture buffer object* (TBO,
// core since GL 3.1 / GLSL 1.40), NOT an SSBO. SSBOs need GL 4.3, which we must
// not assume on the target (grading) machine -- the rest of the engine's GPU
// shaders already target #version 330, so 3.3 is our baseline. Each triangle is
// 7 RGBA32F texels: v0,v1,v2,n0,n1,n2,albedo (w unused). RGBA32F is a mandatory
// texture-buffer format in 3.1+, unlike RGB32F.
static const int TEXELS_PER_TRI = 8;   // v0..v2(.w=u), n0..n2(.w=v), (albedo,km), (texLayer)

static const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// GPU ray-trace fragment shader = [preamble + uniforms + triPos accessor]
//                                + RT_SHADING_GLSL (shared lighting/shadow)
//                                + [closest-hit primary ray + main].
// The triangle TBO holds 7 texels/triangle (v0,v1,v2,n0,n1,n2,albedo): primary
// rays need normals + albedo, while the shared shadow test only reads triPos().
static const char* FRAG_HEAD = R"GLSL(
#version 330 core
out vec4 FragColor;

uniform vec3  uEye, uU, uV, uW;
uniform float uL, uR, uB, uT, uD;
uniform int   uWidth, uHeight, uNumTris, uNumNodes;
#define MAX_LIGHTS 8
uniform int   uNumLights;
uniform vec3  uLightPosArr[MAX_LIGHTS];
uniform vec3  uLightColorArr[MAX_LIGHTS];
uniform vec3  uKs;                    // specular coefficient (per-tri albedo = diffuse)
uniform float uShininess;             // Phong exponent
uniform float uReflMul;               // global mirror-reflection multiplier
uniform samplerBuffer uTris;          // 8 texels per triangle (see C++ side)
uniform samplerBuffer uNodes;         // BVH: 2 texels per node (bbMin|left, bbMax|count)
uniform samplerBuffer uTriIdx;        // BVH leaf -> triangle index (R32F)
uniform sampler2DArray uTexArr;       // per-instance diffuse textures (layer in texel 7.x)
uniform int            uHasTex;

vec3 triTexel(int tri, int slot) { return texelFetch(uTris, tri * 8 + slot).xyz; }
vec3 triPos  (int tri, int slot) { return triTexel(tri, slot); }   // shadow accessor

// Diffuse albedo at a barycentric hit: textured (UV packed in pos.w/normal.w +
// layer in texel 7.x) or the flat per-triangle albedo.
vec3 hitAlbedo(int tri, float hu, float hv) {
    vec3 base = triTexel(tri, 6);
    int  li   = int(texelFetch(uTris, tri * 8 + 7).x);
    if (uHasTex == 0 || li < 0) return base;
    float a = 1.0 - hu - hv;
    float u = a * texelFetch(uTris, tri*8+0).w + hu * texelFetch(uTris, tri*8+1).w + hv * texelFetch(uTris, tri*8+2).w;
    float v = a * texelFetch(uTris, tri*8+3).w + hu * texelFetch(uTris, tri*8+4).w + hv * texelFetch(uTris, tri*8+5).w;
    return texture(uTexArr, vec3(u, v, float(li))).rgb;
}
)GLSL";

static const char* FRAG_BODY = R"GLSL(
// Closest-hit needs barycentrics (for normal interpolation), so it keeps its own
// triangle test; the AABB slab + any-hit shadow come from the shared block.
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

// Stack-based BVH closest-hit traversal (1:1 port of the CPU BVH).
bool traceClosest(vec3 ro, vec3 rd, out float t, out int tri, out float u, out float v) {
    vec3 invD = 1.0 / rd;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    t = 1e30; tri = -1;
    while (sp > 0) {
        int ni = stack[--sp];
        vec4 a = texelFetch(uNodes, ni * 2 + 0);
        vec4 b = texelFetch(uNodes, ni * 2 + 1);
        if (!_slab(ro, invD, a.xyz, b.xyz, t)) continue;
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

// GI sample radiance (GPU RT): uGIBounces<=0 -> ambient occlusion (sky or black);
// otherwise path-trace up to uGIBounces diffuse bounces, each gathering the bounce
// surface's direct lighting (color bleed), the final miss adding the sky.
vec3 giSampleRadiance(vec3 ro, vec3 dir) {
    if (uGIBounces <= 0)
        return occluded(ro, dir, 1.0e9) ? vec3(0.0) : skyColor(dir);

    vec3 thru = vec3(1.0), acc = vec3(0.0);
    vec3 o = ro, d = dir;
    for (int b = 0; b < uGIBounces; ++b) {
        float t; int h; float u, v;
        if (!traceClosest(o, d, t, h, u, v)) { acc += thru * skyColor(d); break; }
        vec3 hp  = o + t * d;
        vec3 hn  = normalize((1.0-u-v) * triTexel(h,3) + u * triTexel(h,4) + v * triTexel(h,5));
        vec3 hng = normalize(cross(triTexel(h,1) - triTexel(h,0), triTexel(h,2) - triTexel(h,0)));
        if (dot(hng, hn) < 0.0) hng = -hng;
        vec3 ha  = hitAlbedo(h, u, v);
        acc  += thru * directLight(hp, hn, ha);    // direct lighting at the bounce
        thru *= ha;                                // attenuate for the next bounce
        float r1 = _giHash(hp.xy + vec2(float(b) * 7.3, 1.7));
        float r2 = _giHash(hp.zx + vec2(float(b) * 3.1, 2.9));
        d = _giCosHemi(hng, r1, r2);
        o = hp + hng * (2e-3 + 1e-3 * length(hp - uEye));
    }
    return acc;
}

void main() {
    // uL/uR/uB/uT already include the aspect ratio (ACamera::SetFOV bakes it into
    // l/r), matching the rasterizer's MakeProjFCG -- do NOT multiply by aspect again.
    float su = uL + (uR - uL) * (gl_FragCoord.x / float(uWidth));
    float sv = uB + (uT - uB) * (gl_FragCoord.y / float(uHeight));
    vec3 rd = normalize(-uD * uW + su * uU + sv * uV);   // ACamera convention
    vec3 ro = uEye;

    // Primary ray finds the visible surface; lighting + shadow is the SHARED
    // ray-traced pass (identical shadeSurface() as the hybrid renderer).
    float closest; int hit; float hu, hv;
    if (!traceClosest(ro, rd, closest, hit, hu, hv)) {
        FragColor = vec4(tonemap(skyColor(rd)), 1.0); return;
    }

    vec3 n0 = triTexel(hit, 3);
    vec3 n1 = triTexel(hit, 4);
    vec3 n2 = triTexel(hit, 5);
    vec3 n  = normalize((1.0 - hu - hv) * n0 + hu * n1 + hv * n2);
    vec3 ng = normalize(cross(triTexel(hit,1) - triTexel(hit,0), triTexel(hit,2) - triTexel(hit,0))); // face normal
    vec3 albedo = hitAlbedo(hit, hu, hv);            // textured or flat diffuse
    float km = texelFetch(uTris, hit * 8 + 6).w * uReflMul;   // mirror reflectance * global multiplier
    vec3 hitPos = ro + closest * rd;

    vec3 col = shadeSurface(hitPos, n, albedo, ng);

    // One mirror bounce: reflect the camera ray and shade what it hits (or sky),
    // then blend by the material's mirror factor km (Blinn-Phong + reflection).
    if (km > 0.001) {
        vec3 rd2 = reflect(rd, n);
        vec3 ro2 = hitPos + n * (2e-3 + 1e-3 * length(hitPos - uEye));   // distance-scaled (no acne)
        float c2; int hit2; float u2, v2;
        vec3 rcol;
        if (traceClosest(ro2, rd2, c2, hit2, u2, v2)) {
            vec3 rn = normalize((1.0 - u2 - v2) * triTexel(hit2,3) + u2 * triTexel(hit2,4) + v2 * triTexel(hit2,5));
            vec3 rng = normalize(cross(triTexel(hit2,1) - triTexel(hit2,0), triTexel(hit2,2) - triTexel(hit2,0)));
            rcol = shadeSurface(ro2 + c2 * rd2, rn, hitAlbedo(hit2, u2, v2), rng);
        } else {
            rcol = skyColor(rd2);
        }
        col = mix(col, rcol, clamp(km, 0.0, 1.0));
    }

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
    const std::string frag = std::string(FRAG_HEAD) + RT_SHADING_GLSL + FRAG_BODY;
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag.c_str());
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
    const glm::vec3 lp = lightPos_.empty()   ? glm::vec3(6, 8, 2) : lightPos_[0];
    const glm::vec3 lc = lightColor_.empty() ? glm::vec3(1)       : lightColor_[0];
    const glm::vec3 km = mesh.material.km;
    const float kmS = glm::max(km.x, glm::max(km.y, km.z));
    UploadWorld({ &mesh }, { model }, { mesh.material.kd }, lp, lc, { kmS }, { &mesh.material });
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
                                 const glm::vec3& lightPos, const glm::vec3& lightColor,
                                 const std::vector<float>& mirrors,
                                 const std::vector<const Material*>& mats)
{
    SetLight(lightPos, lightColor);
    mat_.ks = glm::vec3(0.35f); mat_.shininess = 32.0f;

    // Per-instance diffuse-texture layer (-1 = untextured). Distinct textured
    // instances are resized into a common-size GL_TEXTURE_2D_ARRAY layer.
    const int TS = 512;
    std::vector<int>   instLayer(meshes.size(), -1);
    std::vector<float> arrPixels;                       // TS*TS*3 floats per layer
    int layers = 0;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        if (i >= mats.size() || !mats[i] || mats[i]->texData.empty()) continue;
        const Material& mt = *mats[i];
        const int tw = mt.texWidth, th = mt.texHeight, tc = mt.texChannels > 0 ? mt.texChannels : 3;
        if (tw <= 0 || th <= 0) continue;
        instLayer[i] = layers++;
        for (int y = 0; y < TS; ++y)
        for (int x = 0; x < TS; ++x)
        {
            const int sx = std::min(x * tw / TS, tw - 1);
            const int sy = std::min(y * th / TS, th - 1);
            const size_t si = ((size_t)sy * tw + sx) * tc;
            float r = 0, g = 0, b = 0;
            if (si + 2 < mt.texData.size()) { r = mt.texData[si] / 255.0f; g = mt.texData[si+1] / 255.0f; b = mt.texData[si+2] / 255.0f; }
            else if (si < mt.texData.size()) { r = g = b = mt.texData[si] / 255.0f; }
            // sRGB -> linear (matches CPU SampleDiffuse)
            arrPixels.push_back(std::pow(r, 2.2f)); arrPixels.push_back(std::pow(g, 2.2f)); arrPixels.push_back(std::pow(b, 2.2f));
        }
    }
    texLayers_ = layers;
    if (layers > 0)
    {
        if (!texArr_) glGenTextures(1, &texArr_);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArr_);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB16F, TS, TS, layers, 0, GL_RGB, GL_FLOAT, arrPixels.data());
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    // Combine all instances into one world-space mesh (+ per-triangle albedo +
    // mirror + texture layer), then build a BVH over it for GPU traversal.
    UMesh combined;
    std::vector<glm::vec3> triAlbedo;
    std::vector<float>     triMirror;
    std::vector<float>     triLayer;
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
        const float km = (i < mirrors.size()) ? mirrors[i] : 0.0f;
        const float ly = (float)instLayer[i];
        for (int t = 0; t < m.triangleCount(); ++t) { triAlbedo.push_back(albedos[i]); triMirror.push_back(km); triLayer.push_back(ly); }
    }
    numTris_ = combined.triangleCount();
    combined.BuildBVH();

    // Bake 8 texels per triangle (combined order; BVH leaves index into this).
    // UVs are packed into the .w of the position (u) and normal (v) texels.
    std::vector<glm::vec4> texels;
    texels.reserve(static_cast<size_t>(numTris_) * TEXELS_PER_TRI);
    for (int t = 0; t < numTris_; ++t)
    {
        const Vertex& a = combined.vertices[combined.indices[3 * t + 0]];
        const Vertex& b = combined.vertices[combined.indices[3 * t + 1]];
        const Vertex& c = combined.vertices[combined.indices[3 * t + 2]];
        texels.emplace_back(a.position, a.uv.x); texels.emplace_back(b.position, b.uv.x); texels.emplace_back(c.position, c.uv.x);
        texels.emplace_back(a.normal,   a.uv.y); texels.emplace_back(b.normal,   b.uv.y); texels.emplace_back(c.normal,   c.uv.y);
        texels.emplace_back(triAlbedo[t], triMirror[t]);   // .w = mirror km
        texels.emplace_back(triLayer[t], 0.0f, 0.0f, 0.0f); // .x = diffuse texture layer (-1=none)
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
    const int nL = (int)std::min(lightPos_.size(), (size_t)8);
    glUniform1i(glGetUniformLocation(prog_, "uNumLights"), nL);
    if (nL > 0)
    {
        glUniform3fv(glGetUniformLocation(prog_, "uLightPosArr"),   nL, glm::value_ptr(lightPos_[0]));
        glUniform3fv(glGetUniformLocation(prog_, "uLightColorArr"), nL, glm::value_ptr(lightColor_[0]));
    }
    glUniform3fv(glGetUniformLocation(prog_, "uKs"), 1, glm::value_ptr(mat_.ks));
    glUniform1f (glGetUniformLocation(prog_, "uShininess"), shininess_);
    glUniform1f (glGetUniformLocation(prog_, "uReflMul"), reflMul_);
    glUniform1i (glGetUniformLocation(prog_, "uShadowSamples"), shadowSamples_);
    glUniform1f (glGetUniformLocation(prog_, "uShadowSoftness"), shadowSoftness_);
    glUniform1i (glGetUniformLocation(prog_, "uGIBounces"), giBounces_);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, tex_);
    glUniform1i(glGetUniformLocation(prog_, "uTris"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER, nodeTex_);
    glUniform1i(glGetUniformLocation(prog_, "uNodes"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER, idxTex_);
    glUniform1i(glGetUniformLocation(prog_, "uTriIdx"), 2);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, skyTex_);
    glUniform1i(glGetUniformLocation(prog_, "uSky"), 7);
    glUniform1i(glGetUniformLocation(prog_, "uHasSky"), skyTex_ ? 1 : 0);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D_ARRAY, texArr_);
    glUniform1i(glGetUniformLocation(prog_, "uTexArr"), 4);
    glUniform1i(glGetUniformLocation(prog_, "uHasTex"), texLayers_ > 0 ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog_, "uGISamples"), giSamples_);
    glUniform3fv(glGetUniformLocation(prog_, "uEnvTint"),    1, glm::value_ptr(envTint_));
    glUniform3fv(glGetUniformLocation(prog_, "uSkyHorizon"), 1, glm::value_ptr(skyHorizon_));
    glUniform3fv(glGetUniformLocation(prog_, "uSkyZenith"),  1, glm::value_ptr(skyZenith_));
    glUniform1f (glGetUniformLocation(prog_, "uSkyExp"), skyExp_);

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
    if (texArr_)  { glDeleteTextures(1, &texArr_); texArr_ = 0; texLayers_ = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);  vbo_  = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (prog_)    { glDeleteProgram(prog_); prog_ = 0; }
}
