#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#include "UHybridPass.h"
#include "UGBuffer.h"
#include "ACamera.h"

// Bundled GLEW header predates GL_TEXTURE_BUFFER usage in some configs; the
// constant is present (4.2 header) but define defensively.
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#endif

static const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* FRAG_SRC = R"GLSL(
#version 330 core
out vec4 FragColor;

uniform sampler2D     uWorldPos;     // RGB32F  (per-pixel world position)
uniform sampler2D     uNormal;       // RGB32F  (per-pixel world normal)
uniform sampler2D     uAlbedo;       // RGB32F  (per-pixel albedo = kd)
uniform sampler2D     uDepth;        // R32F    (1.0 = background)
uniform samplerBuffer uTris;         // 3 texels per triangle (v0,v1,v2) for shadow rays

uniform vec3  uEye, uU, uV, uW;
uniform float uL, uR, uB, uT, uD;
uniform int   uWidth, uHeight, uNumTris;
uniform vec3  uLightPos, uLightColor;
uniform vec3  uKs;                    // specular coefficient (Blinn-Phong)
uniform float uShininess;

bool rayTri(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float t) {
    vec3 e1 = v1 - v0, e2 = v2 - v0;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-8) return false;
    float inv = 1.0 / det;
    vec3 s = ro - v0;
    float u = dot(s, p) * inv;            if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    float v = dot(rd, q) * inv;           if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;                 return t > 1e-4;
}

// Any-hit occlusion test up to maxT (shadow ray).
bool occluded(vec3 ro, vec3 rd, float maxT) {
    for (int i = 0; i < uNumTris; ++i) {
        vec3 v0 = texelFetch(uTris, i*3 + 0).xyz;
        vec3 v1 = texelFetch(uTris, i*3 + 1).xyz;
        vec3 v2 = texelFetch(uTris, i*3 + 2).xyz;
        float t;
        if (rayTri(ro, rd, v0, v1, v2, t) && t < maxT - 1e-3) return true;
    }
    return false;
}

vec3 skyColor(vec3 rd) {
    float k = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.10, 0.12, 0.16), vec3(0.40, 0.55, 0.80), k);
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepth, px, 0).r;

    if (depth >= 1.0) {
        // Background: shade the sky along the primary camera ray.
        float aspect = float(uWidth) / float(uHeight);
        float su = (uL + (uR - uL) * (gl_FragCoord.x / float(uWidth))) * aspect;
        float sv =  uB + (uT - uB) * (gl_FragCoord.y / float(uHeight));
        vec3 rd = normalize(-uD * uW + su * uU + sv * uV);
        FragColor = vec4(skyColor(rd), 1.0);
        return;
    }

    vec3 P  = texelFetch(uWorldPos, px, 0).xyz;
    vec3 N  = normalize(texelFetch(uNormal, px, 0).xyz);
    vec3 alb = texelFetch(uAlbedo, px, 0).xyz;

    vec3  toL  = uLightPos - P;
    float dist = length(toL);
    vec3  L    = toL / dist;
    float NdotL = max(dot(N, L), 0.0);

    // one shadow ray toward the light (offset along normal to avoid acne)
    float shadow = (NdotL > 0.0 && occluded(P + N * 1e-3, L, dist)) ? 0.0 : 1.0;

    vec3 Vv = normalize(uEye - P);
    vec3 H  = normalize(L + Vv);
    float NdotH = max(dot(N, H), 0.0);

    // Blinn-Phong direct + Unreal-style sky ambient (env light)
    vec3 ambient = alb * skyColor(N) * 0.35;
    vec3 diffuse = alb * NdotL;
    vec3 spec    = (NdotL > 0.0) ? uKs * pow(NdotH, max(uShininess, 1.0)) : vec3(0.0);
    vec3 col = ambient + (diffuse + spec) * uLightColor * shadow;

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
               std::cerr << "UHybridPass shader error:\n" << buf << "\n"; }
    return id;
}

static GLuint makeTex2D(int w, int h, GLint internalFmt, GLenum fmt)
{
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

void UHybridPass::Init()
{
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    GLint ok; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[4096]; glGetProgramInfoLog(prog_, sizeof(buf), nullptr, buf);
               std::cerr << "UHybridPass link error:\n" << buf << "\n"; }
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

void UHybridPass::UploadSceneTriangles(const std::vector<glm::vec3>& tris)
{
    numTris_ = static_cast<int>(tris.size()) / 3;
    if (!tbo_) glGenBuffers(1, &tbo_);
    glBindBuffer(GL_TEXTURE_BUFFER, tbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(tris.size() * sizeof(glm::vec3)),
                 tris.empty() ? nullptr : glm::value_ptr(tris[0]), GL_STATIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);

    if (!triTex_) glGenTextures(1, &triTex_);
    glBindTexture(GL_TEXTURE_BUFFER, triTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo_);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void UHybridPass::UploadGBuffer(const UGBuffer& gb)
{
    if (gb.nx != gw_ || gb.ny != gh_)        // (re)allocate textures on resize
    {
        if (texWP_)    { glDeleteTextures(1, &texWP_);    texWP_    = 0; }
        if (texN_)     { glDeleteTextures(1, &texN_);     texN_     = 0; }
        if (texAlb_)   { glDeleteTextures(1, &texAlb_);   texAlb_   = 0; }
        if (texDepth_) { glDeleteTextures(1, &texDepth_); texDepth_ = 0; }
        gw_ = gb.nx; gh_ = gb.ny;
        texWP_    = makeTex2D(gw_, gh_, GL_RGB32F, GL_RGB);
        texN_     = makeTex2D(gw_, gh_, GL_RGB32F, GL_RGB);
        texAlb_   = makeTex2D(gw_, gh_, GL_RGB32F, GL_RGB);
        texDepth_ = makeTex2D(gw_, gh_, GL_R32F,   GL_RED);
    }

    glBindTexture(GL_TEXTURE_2D, texWP_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gw_, gh_, GL_RGB, GL_FLOAT, glm::value_ptr(gb.worldPos[0]));
    glBindTexture(GL_TEXTURE_2D, texN_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gw_, gh_, GL_RGB, GL_FLOAT, glm::value_ptr(gb.normal[0]));
    glBindTexture(GL_TEXTURE_2D, texAlb_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gw_, gh_, GL_RGB, GL_FLOAT, glm::value_ptr(gb.albedo[0]));
    glBindTexture(GL_TEXTURE_2D, texDepth_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gw_, gh_, GL_RED, GL_FLOAT, gb.depth.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UHybridPass::Render(const ACamera& cam, const glm::vec3& lightPos,
                         const glm::vec3& lightColor, int width, int height) const
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
    glUniform3fv(glGetUniformLocation(prog_, "uLightPos"),   1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, glm::value_ptr(lightColor));
    glm::vec3 ks(0.5f);
    glUniform3fv(glGetUniformLocation(prog_, "uKs"), 1, glm::value_ptr(ks));
    glUniform1f (glGetUniformLocation(prog_, "uShininess"), 48.0f);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texWP_);
    glUniform1i(glGetUniformLocation(prog_, "uWorldPos"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texN_);
    glUniform1i(glGetUniformLocation(prog_, "uNormal"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texAlb_);
    glUniform1i(glGetUniformLocation(prog_, "uAlbedo"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, texDepth_);
    glUniform1i(glGetUniformLocation(prog_, "uDepth"), 3);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_BUFFER, triTex_);
    glUniform1i(glGetUniformLocation(prog_, "uTris"), 4);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void UHybridPass::Cleanup()
{
    if (texWP_)    glDeleteTextures(1, &texWP_);
    if (texN_)     glDeleteTextures(1, &texN_);
    if (texAlb_)   glDeleteTextures(1, &texAlb_);
    if (texDepth_) glDeleteTextures(1, &texDepth_);
    if (triTex_)   glDeleteTextures(1, &triTex_);
    if (tbo_)      glDeleteBuffers(1, &tbo_);
    if (vbo_)      glDeleteBuffers(1, &vbo_);
    if (vao_)      glDeleteVertexArrays(1, &vao_);
    if (prog_)     glDeleteProgram(prog_);
    texWP_ = texN_ = texAlb_ = texDepth_ = triTex_ = tbo_ = vbo_ = vao_ = prog_ = 0;
}
