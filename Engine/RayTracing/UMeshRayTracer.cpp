#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <vector>

#include "UMeshRayTracer.h"
#include "UMesh.h"
#include "ACamera.h"

// Triangles are passed to the shader through a *texture buffer object* (TBO,
// core since GL 3.1 / GLSL 1.40), NOT an SSBO. SSBOs need GL 4.3, which we must
// not assume on the target (grading) machine -- the rest of the engine's GPU
// shaders already target #version 330, so 3.3 is our baseline. Each triangle is
// 6 RGBA32F texels: v0,v1,v2,n0,n1,n2 (w unused). RGBA32F is a mandatory
// texture-buffer format in 3.1+, unlike RGB32F.
static const int TEXELS_PER_TRI = 6;

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
uniform vec3  uLightDir, uLightColor;
uniform vec3  uKa, uKd, uKs;          // Blinn-Phong material (ambient/diffuse/specular)
uniform float uShininess;             // Phong exponent
uniform samplerBuffer uTris;          // 6 texels per triangle (see C++ side)

vec3 triTexel(int tri, int slot) { return texelFetch(uTris, tri * 6 + slot).xyz; }

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

void main() {
    float aspect = float(uWidth) / float(uHeight);
    float su = (uL + (uR - uL) * (gl_FragCoord.x / float(uWidth))) * aspect;
    float sv =  uB + (uT - uB) * (gl_FragCoord.y / float(uHeight));
    vec3 rd = normalize(-uD * uW + su * uU + sv * uV);   // ACamera convention
    vec3 ro = uEye;

    float closest = 1e30;
    int   hit = -1;
    float hu = 0.0, hv = 0.0;
    for (int i = 0; i < uNumTris; ++i) {
        vec3 v0 = triTexel(i, 0);
        vec3 v1 = triTexel(i, 1);
        vec3 v2 = triTexel(i, 2);
        float t, u, v;
        if (rayTri(ro, rd, v0, v1, v2, t, u, v) && t < closest) {
            closest = t; hit = i; hu = u; hv = v;
        }
    }

    if (hit < 0) { FragColor = vec4(skyColor(rd), 1.0); return; }

    vec3 n0 = triTexel(hit, 3);
    vec3 n1 = triTexel(hit, 4);
    vec3 n2 = triTexel(hit, 5);
    vec3 n  = normalize((1.0 - hu - hv) * n0 + hu * n1 + hv * n2);
    vec3 hitPos = ro + closest * rd;

    // Blinn-Phong direct light + Unreal-style sky ambient (env light approximated
    // by the hemisphere sky in the surface-normal direction).
    vec3 L = normalize(uLightDir);
    vec3 Vv = normalize(uEye - hitPos);
    vec3 H = normalize(L + Vv);
    float NdotL = max(dot(n, L), 0.0);
    float NdotH = max(dot(n, H), 0.0);

    vec3 ambient = uKa * skyColor(n);
    vec3 diffuse = uKd * NdotL;
    vec3 spec    = (NdotL > 0.0) ? uKs * pow(NdotH, max(uShininess, 1.0)) : vec3(0.0);
    vec3 col = ambient + (diffuse + spec) * uLightColor;
    FragColor = vec4(col, 1.0);
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
    const glm::mat3 nrmM = glm::inverseTranspose(glm::mat3(model));
    const int nTri = mesh.triangleCount();
    mat_ = mesh.material;                      // captured for Blinn-Phong shading

    // Flat RGBA32F texel stream: 6 texels per triangle (v0 v1 v2 n0 n1 n2).
    std::vector<glm::vec4> texels;
    texels.reserve(static_cast<size_t>(nTri) * TEXELS_PER_TRI);
    for (int i = 0; i < nTri; ++i)
    {
        const Vertex& a = mesh.vertices[mesh.indices[3 * i + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[3 * i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[3 * i + 2]];
        texels.emplace_back(glm::vec3(model * glm::vec4(a.position, 1.0f)), 0.0f);
        texels.emplace_back(glm::vec3(model * glm::vec4(b.position, 1.0f)), 0.0f);
        texels.emplace_back(glm::vec3(model * glm::vec4(c.position, 1.0f)), 0.0f);
        texels.emplace_back(glm::normalize(nrmM * a.normal), 0.0f);
        texels.emplace_back(glm::normalize(nrmM * b.normal), 0.0f);
        texels.emplace_back(glm::normalize(nrmM * c.normal), 0.0f);
    }
    numTris_ = nTri;

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
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.7f, 0.4f));
    glUniform3fv(glGetUniformLocation(prog_, "uLightDir"), 1, glm::value_ptr(lightDir));
    glm::vec3 lightColor(1.0f);
    glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, glm::value_ptr(lightColor));

    // Blinn-Phong material (engine's existing shading model)
    glUniform3fv(glGetUniformLocation(prog_, "uKa"), 1, glm::value_ptr(mat_.ka));
    glUniform3fv(glGetUniformLocation(prog_, "uKd"), 1, glm::value_ptr(mat_.kd));
    glUniform3fv(glGetUniformLocation(prog_, "uKs"), 1, glm::value_ptr(mat_.ks));
    glUniform1f (glGetUniformLocation(prog_, "uShininess"), mat_.shininess);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, tex_);
    glUniform1i(glGetUniformLocation(prog_, "uTris"), 0);   // sampler unit 0

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

void UMeshRayTracer::Cleanup()
{
    if (tex_)  { glDeleteTextures(1, &tex_); tex_ = 0; }
    if (tbo_)  { glDeleteBuffers(1, &tbo_);  tbo_ = 0; }
    if (vbo_)  { glDeleteBuffers(1, &vbo_);  vbo_  = 0; }
    if (vao_)  { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
}
