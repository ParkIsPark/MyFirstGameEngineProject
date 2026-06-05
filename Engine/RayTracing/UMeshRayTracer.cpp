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
// shaders already target #version 330, so 3.3 is our baseline.
//
// TWO-LEVEL BVH (instancing). Triangles + per-mesh BVHs (BLAS) are stored ONCE in
// MESH-LOCAL space, concatenated across the unique meshes (each mesh records a
// node/tri/triIdx offset). A per-instance record holds the inverse model matrix
// (to transform a ray into the mesh's local space), shading params, the mesh's
// BLAS offsets, and a world-space AABB. Moving an object only rewrites the small
// instance buffer -- the expensive BLAS build/upload is cached -- so dragging a
// high-poly object no longer rebuilds the whole scene's tree every frame.
static const int TEXELS_PER_TRI  = 6;   // v0..v2(.w=u), n0..n2(.w=v)  [LOCAL space]
static const int TEXELS_PER_INST = 7;   // invM r0,r1,r2 | (albedo,km) | (layer,nodeOff,triOff,idxOff) | wbbMin | wbbMax

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
uniform int   uNumInstances;          // two-level BVH: instance count
#define MAX_LIGHTS 8
uniform int   uNumLights;
uniform vec3  uLightPosArr[MAX_LIGHTS];
uniform vec3  uLightColorArr[MAX_LIGHTS];
uniform vec3  uKs;                    // specular coefficient (per-tri albedo = diffuse)
uniform float uShininess;             // Phong exponent
uniform float uReflMul;               // global mirror-reflection multiplier
uniform samplerBuffer uTris;          // 6 texels per triangle, MESH-LOCAL (concat BLAS)
uniform samplerBuffer uNodes;         // concat BLAS nodes: 2 texels/node (bbMin|left, bbMax|count)
uniform samplerBuffer uTriIdx;        // concat BLAS leaf -> mesh-local triangle index (R32F)
uniform samplerBuffer uInstances;     // 7 texels per instance (see C++ side)
uniform sampler2DArray uTexArr;       // per-instance diffuse textures (layer in instance.4.x)
uniform int            uHasTex;

vec3 triTexel(int tri, int slot) { return texelFetch(uTris, tri * 6 + slot).xyz; }
vec3 triPos  (int tri, int slot) { return triTexel(tri, slot); }

// Instance record accessors (7 texels). r0..r2 = rows of the inverse model
// matrix (world -> mesh-local point/vector transforms).
vec4 instR0(int i)   { return texelFetch(uInstances, i * 7 + 0); }
vec4 instR1(int i)   { return texelFetch(uInstances, i * 7 + 1); }
vec4 instR2(int i)   { return texelFetch(uInstances, i * 7 + 2); }
vec4 instMat(int i)  { return texelFetch(uInstances, i * 7 + 3); }  // (albedo.rgb, km)
vec4 instOff(int i)  { return texelFetch(uInstances, i * 7 + 4); }  // (layer, nodeOff, triOff, idxOff)
vec3 instMin(int i)  { return texelFetch(uInstances, i * 7 + 5).xyz; }
vec3 instMax(int i)  { return texelFetch(uInstances, i * 7 + 6).xyz; }

vec3 xfPoint(vec4 r0, vec4 r1, vec4 r2, vec3 p) {
    return vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);
}
vec3 xfDir(vec4 r0, vec4 r1, vec4 r2, vec3 d) {
    return vec3(dot(r0.xyz, d), dot(r1.xyz, d), dot(r2.xyz, d));
}
// Local normal -> world normal = transpose(mat3(invModel)) * n.
vec3 normalToWorld(vec4 r0, vec4 r1, vec4 r2, vec3 n) {
    return vec3(r0.x*n.x + r1.x*n.y + r2.x*n.z,
                r0.y*n.x + r1.y*n.y + r2.y*n.z,
                r0.z*n.x + r1.z*n.y + r2.z*n.z);
}

// Diffuse albedo at a barycentric hit on instance `inst`, triangle `tri` (global
// into the concatenated local triangle buffer). Textured (UV packed in the .w of
// the position/normal texels + layer from the instance) or the flat albedo.
vec3 hitAlbedo(int inst, int tri, float hu, float hv) {
    vec4 m = instMat(inst);
    int  li = int(instOff(inst).x);
    if (uHasTex == 0 || li < 0) return m.xyz;
    float a = 1.0 - hu - hv;
    float u = a * texelFetch(uTris, tri*6+0).w + hu * texelFetch(uTris, tri*6+1).w + hv * texelFetch(uTris, tri*6+2).w;
    float v = a * texelFetch(uTris, tri*6+3).w + hu * texelFetch(uTris, tri*6+4).w + hv * texelFetch(uTris, tri*6+5).w;
    return texture(uTexArr, vec3(u, v, float(li))).rgb;
}
)GLSL";

static const char* FRAG_BODY = R"GLSL(
// Closest-hit needs barycentrics (for normal interpolation), so it keeps its own
// triangle test; the AABB slab + ray-tri occlusion helpers come from the shared block.
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

// ---- BLAS traversal (one mesh, in its LOCAL space). nodeOff/triOff/idxOff are
// the mesh's bases into the concatenated node / triangle / triIndex buffers. The
// closest distance `t` is carried IN/OUT so it stays the global nearest across
// instances (the local ray is the world ray pushed through invModel, so the same
// parameter t is comparable in either space). ----
bool blasClosest(vec3 lo, vec3 ld, int nodeOff, int triOff, int idxOff,
                 inout float t, out int triHit, out float u, out float v) {
    vec3 invD = 1.0 / ld;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    bool hit = false; triHit = -1;
    while (sp > 0) {
        int ni = nodeOff + stack[--sp];
        vec4 a = texelFetch(uNodes, ni * 2 + 0);
        vec4 b = texelFetch(uNodes, ni * 2 + 1);
        if (!_slab(lo, invD, a.xyz, b.xyz, t)) continue;
        int rc = int(b.w);
        if (rc > 0) {                                   // leaf
            int start = int(a.w);
            for (int i = 0; i < rc; ++i) {
                int g = triOff + int(texelFetch(uTriIdx, idxOff + start + i).x);
                float tt, uu, vv;
                if (rayTri(lo, ld, triTexel(g,0), triTexel(g,1), triTexel(g,2), tt, uu, vv) && tt < t) {
                    t = tt; triHit = g; u = uu; v = vv; hit = true;
                }
            }
        } else if (sp + 2 <= 64) {                      // inner
            stack[sp++] = int(a.w);
            stack[sp++] = -rc;
        }
    }
    return hit;
}

bool blasOccluded(vec3 lo, vec3 ld, int nodeOff, int triOff, int idxOff, float maxT, float tMin) {
    vec3 invD = 1.0 / ld;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        int ni = nodeOff + stack[--sp];
        vec4 a = texelFetch(uNodes, ni * 2 + 0);
        vec4 b = texelFetch(uNodes, ni * 2 + 1);
        if (!_slab(lo, invD, a.xyz, b.xyz, maxT)) continue;
        int rc = int(b.w);
        if (rc > 0) {
            int start = int(a.w);
            for (int i = 0; i < rc; ++i) {
                int g = triOff + int(texelFetch(uTriIdx, idxOff + start + i).x);
                float tt;
                if (_rayTriT(lo, ld, triTexel(g,0), triTexel(g,1), triTexel(g,2), tt)
                    && tt > tMin && tt < maxT - 1e-3) return true;
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = int(a.w);
            stack[sp++] = -rc;
        }
    }
    return false;
}

// ---- TLAS = a linear loop over instances (scenes have few objects). Each
// instance is world-AABB rejected, then the ray is pushed into mesh-local space
// and traced against the (cached) BLAS. Returns the global triangle + instance. ----
bool traceClosest(vec3 ro, vec3 rd, out float t, out int tri, out float u, out float v, out int inst) {
    vec3 invD = 1.0 / rd;
    t = 1e30; tri = -1; inst = -1; bool found = false;
    for (int i = 0; i < uNumInstances; ++i) {
        if (!_slab(ro, invD, instMin(i), instMax(i), t)) continue;
        vec4 r0 = instR0(i), r1 = instR1(i), r2 = instR2(i), off = instOff(i);
        vec3 lo = xfPoint(r0, r1, r2, ro);
        vec3 ld = xfDir  (r0, r1, r2, rd);
        int th; float uu, vv;
        if (blasClosest(lo, ld, int(off.y), int(off.z), int(off.w), t, th, uu, vv)) {
            tri = th; u = uu; v = vv; inst = i; found = true;
        }
    }
    return found;
}

// Two-level any-hit occlusion (shadow / GI rays). Definition for the prototype in
// RTShading.h -- the shared shadeSurface/directLight call this.
bool occluded(vec3 ro, vec3 rd, float maxT, float tMin) {
    vec3 invD = 1.0 / rd;
    for (int i = 0; i < uNumInstances; ++i) {
        if (!_slab(ro, invD, instMin(i), instMax(i), maxT)) continue;
        vec4 r0 = instR0(i), r1 = instR1(i), r2 = instR2(i), off = instOff(i);
        vec3 lo = xfPoint(r0, r1, r2, ro);
        vec3 ld = xfDir  (r0, r1, r2, rd);
        if (blasOccluded(lo, ld, int(off.y), int(off.z), int(off.w), maxT, tMin)) return true;
    }
    return false;
}

// World-space shading attributes at a hit (transform local normals to world).
vec3 hitNormal(int inst, int tri, float hu, float hv) {
    vec4 r0 = instR0(inst), r1 = instR1(inst), r2 = instR2(inst);
    vec3 ln = (1.0 - hu - hv) * triTexel(tri,3) + hu * triTexel(tri,4) + hv * triTexel(tri,5);
    return normalize(normalToWorld(r0, r1, r2, ln));
}
vec3 hitFaceNormal(int inst, int tri) {
    vec4 r0 = instR0(inst), r1 = instR1(inst), r2 = instR2(inst);
    vec3 lng = cross(triTexel(tri,1) - triTexel(tri,0), triTexel(tri,2) - triTexel(tri,0));
    return normalize(normalToWorld(r0, r1, r2, lng));
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
        float t; int h; float u, v; int hi;
        if (!traceClosest(o, d, t, h, u, v, hi)) { acc += thru * skyColor(d); break; }
        vec3 hp  = o + t * d;
        vec3 hn  = hitNormal(hi, h, u, v);
        vec3 hng = hitFaceNormal(hi, h);
        if (dot(hng, hn) < 0.0) hng = -hng;
        vec3 ha  = hitAlbedo(hi, h, u, v);
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
    float closest; int hit; float hu, hv; int inst;
    if (!traceClosest(ro, rd, closest, hit, hu, hv, inst)) {
        FragColor = vec4(tonemap(skyColor(rd)), 1.0); return;
    }

    vec3 n  = hitNormal(inst, hit, hu, hv);
    vec3 ng = hitFaceNormal(inst, hit);              // world-space face normal
    vec3 albedo = hitAlbedo(inst, hit, hu, hv);      // textured or flat diffuse
    float km = instMat(inst).w * uReflMul;           // mirror reflectance * global multiplier
    vec3 hitPos = ro + closest * rd;

    vec3 col = shadeSurface(hitPos, n, albedo, ng);

    // One mirror bounce: reflect the camera ray and shade what it hits (or sky),
    // then blend by the material's mirror factor km (Blinn-Phong + reflection).
    if (km > 0.001) {
        vec3 rd2 = reflect(rd, n);
        vec3 ro2 = hitPos + n * (2e-3 + 1e-3 * length(hitPos - uEye));   // distance-scaled (no acne)
        float c2; int hit2; float u2, v2; int inst2;
        vec3 rcol;
        if (traceClosest(ro2, rd2, c2, hit2, u2, v2, inst2)) {
            rcol = shadeSurface(ro2 + c2 * rd2, hitNormal(inst2, hit2, u2, v2),
                                hitAlbedo(inst2, hit2, u2, v2), hitFaceNormal(inst2, hit2));
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

void UMeshRayTracer::uploadBufferTex(unsigned int& tbo, unsigned int& tex, unsigned int fmt,
                                     const void* data, size_t bytes)
{
    if (!tbo) glGenBuffers(1, &tbo);
    glBindBuffer(GL_TEXTURE_BUFFER, tbo);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)bytes, bytes ? data : nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_BUFFER, tex);
    glTexBuffer(GL_TEXTURE_BUFFER, fmt, tbo);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

// Build (once) and cache a mesh's BLAS: a mesh-LOCAL BVH plus the GPU texel arrays
// (6 texels/triangle, 2/node, leaf->local-tri index) and the local AABB. Shared by
// every instance of the mesh; never rebuilt while the mesh exists.
const UMeshRayTracer::MeshBlas& UMeshRayTracer::ensureBlas(const UMesh& mesh)
{
    auto it = blas_.find(&mesh);
    if (it != blas_.end()) return it->second;

    MeshBlas mb;
    BVH bvh;
    bvh.Build(mesh);
    mb.numNodes = (int)bvh.nodes.size();
    mb.numTris  = mesh.triangleCount();

    mb.nodeTexels.reserve(bvh.nodes.size() * 2);
    for (const BVHNode& n : bvh.nodes)
    {
        mb.nodeTexels.emplace_back(n.bbMin, (float)n.leftOrTriStart);
        mb.nodeTexels.emplace_back(n.bbMax, (float)n.rightOrTriCount);
    }
    mb.triIdx.assign(bvh.triIndices.begin(), bvh.triIndices.end());

    // 6 texels/triangle in MESH-LOCAL space (positions + normals, uv packed in .w).
    mb.triTexels.reserve((size_t)mb.numTris * TEXELS_PER_TRI);
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (int t = 0; t < mb.numTris; ++t)
    {
        const Vertex& a = mesh.vertices[mesh.indices[3 * t + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[3 * t + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[3 * t + 2]];
        mb.triTexels.emplace_back(a.position, a.uv.x);
        mb.triTexels.emplace_back(b.position, b.uv.x);
        mb.triTexels.emplace_back(c.position, c.uv.x);
        mb.triTexels.emplace_back(a.normal,   a.uv.y);
        mb.triTexels.emplace_back(b.normal,   b.uv.y);
        mb.triTexels.emplace_back(c.normal,   c.uv.y);
    }
    for (const Vertex& v : mesh.vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
    if (mesh.vertices.empty()) { mn = glm::vec3(0.0f); mx = glm::vec3(0.0f); }
    mb.bbMin = mn; mb.bbMax = mx;

    return blas_.emplace(&mesh, std::move(mb)).first->second;
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

    // Signature of the cached (expensive) part: which meshes + which textures. When
    // unchanged, only the cheap per-instance buffer is rebuilt below -- so moving an
    // object never re-uploads the BLAS triangles/BVH (the source of drag stutter).
    size_t sig = 1469598103934665603ull;
    auto mix = [&](size_t v) { sig ^= v; sig *= 1099511628211ull; };
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        mix((size_t)meshes[i]);
        const Material* mt = (i < mats.size()) ? mats[i] : nullptr;
        mix(mt ? mt->texData.size() : 0);
        mix(mt ? (size_t)(mt->texWidth * 73856093 ^ mt->texHeight * 19349663) : 0);
    }

    if (!blasUploaded_ || sig != blasSig_)
    {
        // ---- Diffuse-texture array: one layer per textured instance. ----
        const int TS = 512;
        instLayer_.assign(meshes.size(), -1);
        std::vector<float> arrPixels;
        int layers = 0;
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            if (i >= mats.size() || !mats[i] || mats[i]->texData.empty()) continue;
            const Material& mt = *mats[i];
            const int tw = mt.texWidth, th = mt.texHeight, tc = mt.texChannels > 0 ? mt.texChannels : 3;
            if (tw <= 0 || th <= 0) continue;
            instLayer_[i] = layers++;
            for (int y = 0; y < TS; ++y)
            for (int x = 0; x < TS; ++x)
            {
                const int sx = std::min(x * tw / TS, tw - 1);
                const int sy = std::min(y * th / TS, th - 1);
                const size_t si = ((size_t)sy * tw + sx) * tc;
                float r = 0, g = 0, b = 0;
                if (si + 2 < mt.texData.size()) { r = mt.texData[si]/255.0f; g = mt.texData[si+1]/255.0f; b = mt.texData[si+2]/255.0f; }
                else if (si < mt.texData.size()) { r = g = b = mt.texData[si]/255.0f; }
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

        // ---- Concatenate every unique mesh's cached BLAS, recording offsets. ----
        std::vector<glm::vec4> nodeAll, triAll;
        std::vector<float>     idxAll;
        meshOff_.clear();
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            if (!meshes[i] || meshOff_.count(meshes[i])) continue;     // unique meshes only
            const MeshBlas& mb = ensureBlas(*meshes[i]);
            MeshOff off;
            off.nodeOff = (int)(nodeAll.size() / 2);
            off.triOff  = (int)(triAll.size()  / TEXELS_PER_TRI);
            off.idxOff  = (int)idxAll.size();
            meshOff_[meshes[i]] = off;
            nodeAll.insert(nodeAll.end(), mb.nodeTexels.begin(), mb.nodeTexels.end());
            triAll.insert (triAll.end(),  mb.triTexels.begin(),  mb.triTexels.end());
            idxAll.insert (idxAll.end(),  mb.triIdx.begin(),      mb.triIdx.end());
        }
        numTris_  = (int)(triAll.size()  / TEXELS_PER_TRI);
        numNodes_ = (int)(nodeAll.size() / 2);
        uploadBufferTex(tbo_,     tex_,     GL_RGBA32F, triAll.data(),  triAll.size()  * sizeof(glm::vec4));
        uploadBufferTex(nodeTbo_, nodeTex_, GL_RGBA32F, nodeAll.data(), nodeAll.size() * sizeof(glm::vec4));
        uploadBufferTex(idxTbo_,  idxTex_,  GL_R32F,    idxAll.data(),  idxAll.size()  * sizeof(float));

        blasSig_ = sig; blasUploaded_ = true;
    }

    // ---- Per-instance records (rebuilt every call -- this is the cheap part). ----
    std::vector<glm::vec4> inst;
    inst.reserve(meshes.size() * TEXELS_PER_INST);
    numInstances_ = 0;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        if (!meshes[i]) continue;
        const MeshOff& off = meshOff_[meshes[i]];
        const MeshBlas& mb = ensureBlas(*meshes[i]);
        const glm::mat4 invM = glm::inverse(models[i]);
        // rows of invM (point/vector world->local): localP = row . worldP (+ row.w)
        inst.emplace_back(invM[0][0], invM[1][0], invM[2][0], invM[3][0]);
        inst.emplace_back(invM[0][1], invM[1][1], invM[2][1], invM[3][1]);
        inst.emplace_back(invM[0][2], invM[1][2], invM[2][2], invM[3][2]);
        const glm::vec3 alb = (i < albedos.size()) ? albedos[i] : glm::vec3(0.8f);
        const float km  = (i < mirrors.size()) ? mirrors[i] : 0.0f;
        inst.emplace_back(alb, km);
        inst.emplace_back((float)instLayer_[i], (float)off.nodeOff, (float)off.triOff, (float)off.idxOff);
        // World-space AABB of the instance (transform the 8 local corners).
        glm::vec3 wmn(1e30f), wmx(-1e30f);
        for (int c = 0; c < 8; ++c)
        {
            const glm::vec3 corner((c & 1) ? mb.bbMax.x : mb.bbMin.x,
                                   (c & 2) ? mb.bbMax.y : mb.bbMin.y,
                                   (c & 4) ? mb.bbMax.z : mb.bbMin.z);
            const glm::vec3 w = glm::vec3(models[i] * glm::vec4(corner, 1.0f));
            wmn = glm::min(wmn, w); wmx = glm::max(wmx, w);
        }
        inst.emplace_back(wmn, 0.0f);
        inst.emplace_back(wmx, 0.0f);
        ++numInstances_;
    }
    uploadBufferTex(instTbo_, instTex_, GL_RGBA32F, inst.data(), inst.size() * sizeof(glm::vec4));
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
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_BUFFER, instTex_);
    glUniform1i(glGetUniformLocation(prog_, "uInstances"), 5);
    glUniform1i(glGetUniformLocation(prog_, "uNumInstances"), numInstances_);
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
    if (instTex_) { glDeleteTextures(1, &instTex_); instTex_ = 0; }
    if (instTbo_) { glDeleteBuffers(1, &instTbo_);  instTbo_ = 0; }
    if (texArr_)  { glDeleteTextures(1, &texArr_); texArr_ = 0; texLayers_ = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);  vbo_  = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (prog_)    { glDeleteProgram(prog_); prog_ = 0; }
}
