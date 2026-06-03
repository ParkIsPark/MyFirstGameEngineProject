#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#include "UHybridPass.h"
#include "UGBuffer.h"
#include "ACamera.h"
#include "UMesh.h"
#include "BVH.h"
#include "RTShading.h"     // shared lighting+shadow GLSL (same code as GPU RT)

#include <string>

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

// Hybrid fragment shader = [preamble + uniforms + triPos accessor]
//                          + RT_SHADING_GLSL (shared lighting/shadow)
//                          + [main: read primary hit from the G-buffer].
// The triangle TBO holds 3 texels/triangle (positions only) since shadow rays
// need geometry, not shading attributes.
static const char* FRAG_HEAD = R"GLSL(
#version 330 core
out vec4 FragColor;

uniform sampler2D     uWorldPos;     // RGB32F  (per-pixel world position)
uniform sampler2D     uNormal;       // RGB32F  (per-pixel world normal)
uniform sampler2D     uAlbedo;       // RGB32F  (per-pixel albedo = kd)
uniform sampler2D     uDepth;        // R32F    (1.0 = background)
uniform samplerBuffer uTris;         // 3 texels per triangle (v0,v1,v2)
uniform samplerBuffer uNodes;        // BVH: 2 texels/node (bbMin|left, bbMax|count)
uniform samplerBuffer uTriIdx;       // BVH leaf -> triangle index (R32F)

uniform vec3  uEye, uU, uV, uW;
uniform float uL, uR, uB, uT, uD;
uniform int   uWidth, uHeight, uNumTris, uNumNodes;
uniform vec3  uLightPos, uLightColor;
uniform vec3  uKs;                    // specular coefficient (Blinn-Phong)
uniform float uShininess;

vec3 triPos(int tri, int slot) { return texelFetch(uTris, tri * 3 + slot).xyz; }
)GLSL";

static const char* FRAG_MAIN = R"GLSL(
void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepth, px, 0).r;

    // RASTER decided visibility: depth==1 means this pixel saw no surface, so
    // shade the sky along the primary camera ray.
    if (depth >= 1.0) {
        float aspect = float(uWidth) / float(uHeight);
        float su = (uL + (uR - uL) * (gl_FragCoord.x / float(uWidth))) * aspect;
        float sv =  uB + (uT - uB) * (gl_FragCoord.y / float(uHeight));
        vec3 rd = normalize(-uD * uW + su * uU + sv * uV);
        FragColor = vec4(tonemap(skyColor(rd)), 1.0);
        return;
    }

    // Primary hit comes from the rasterized G-buffer; the lighting + shadow is
    // the SHARED ray-traced pass (identical to GPU RT mode's shadeSurface()).
    vec3 P   = texelFetch(uWorldPos, px, 0).xyz;
    vec3 N   = normalize(texelFetch(uNormal, px, 0).xyz);
    vec3 alb = texelFetch(uAlbedo,   px, 0).xyz;
    FragColor = vec4(tonemap(shadeSurface(P, N, alb)), 1.0);
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
    const std::string frag = std::string(FRAG_HEAD) + RT_SHADING_GLSL + FRAG_MAIN;
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag.c_str());
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

    // Triangle TBO (original order; BVH leaves index into this).
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

    if (numTris_ == 0) { numNodes_ = 0; return; }

    // Build a BVH over the world-space shadow triangles (same accel as GPU RT).
    UMesh m;
    m.vertices.reserve(tris.size());
    for (const glm::vec3& p : tris) { Vertex v; v.position = p; m.vertices.push_back(v); }
    m.indices.reserve(tris.size());
    for (uint32_t i = 0; i < tris.size(); ++i) m.indices.push_back(i);
    m.BuildBVH();
    const BVH& bvh = *m.bvh;

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
    glUniform1i (glGetUniformLocation(prog_, "uNumNodes"), numNodes_);
    glUniform3fv(glGetUniformLocation(prog_, "uLightPos"),   1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, glm::value_ptr(lightColor));
    glm::vec3 ks(0.35f);   // match GPU RT mode (UMeshRayTracer) so the shared
    glUniform3fv(glGetUniformLocation(prog_, "uKs"), 1, glm::value_ptr(ks));
    glUniform1f (glGetUniformLocation(prog_, "uShininess"), 32.0f);   // shadeSurface() looks identical

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
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_BUFFER, nodeTex_);
    glUniform1i(glGetUniformLocation(prog_, "uNodes"), 5);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_BUFFER, idxTex_);
    glUniform1i(glGetUniformLocation(prog_, "uTriIdx"), 6);

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
    if (nodeTex_)  glDeleteTextures(1, &nodeTex_);
    if (nodeTbo_)  glDeleteBuffers(1, &nodeTbo_);
    if (idxTex_)   glDeleteTextures(1, &idxTex_);
    if (idxTbo_)   glDeleteBuffers(1, &idxTbo_);
    if (vbo_)      glDeleteBuffers(1, &vbo_);
    if (vao_)      glDeleteVertexArrays(1, &vao_);
    if (prog_)     glDeleteProgram(prog_);
    texWP_ = texN_ = texAlb_ = texDepth_ = triTex_ = tbo_ = vbo_ = vao_ = prog_ = 0;
    nodeTex_ = nodeTbo_ = idxTex_ = idxTbo_ = 0;
}
