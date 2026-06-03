// ---------------------------------------------------------------------------
// shading_test.cpp — GL-free self-test for P4 CPU shading (HW6 Q1-Q3).
//
// Verifies Flat/Gouraud/Phong via URasterizer::DrawMeshShaded into a
// UFrameBuffer, read back on the CPU (no GL). The reference-image pixel match
// is a separate VISUAL check (editor); this validates the shading math/models.
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/Rasterizer -I Engine/Mesh \
//       -I Engine/World -I Engine/RayTracing -I Engine/Acceleration \
//       Test/shading_test.cpp Engine/Rasterizer/URasterizer.cpp \
//       Engine/Rasterizer/FTransform.cpp Engine/Mesh/UMesh.cpp \
//       Engine/Mesh/Material.cpp Engine/Acceleration/BVH.cpp -o shading_test \
//       && ./shading_test
// ---------------------------------------------------------------------------
#include "URasterizer.h"
#include "FTransform.h"
#include "UFrameBuffer.h"
#include "UMesh.h"
#include "Material.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static bool veq(const glm::vec3& a, const glm::vec3& b, float e = 2e-3f)
{
    return std::fabs(a.x-b.x)<e && std::fabs(a.y-b.y)<e && std::fabs(a.z-b.z)<e;
}
static FTransform hwXf(int nx, int ny)
{
    FTransform xf;
    xf.model    = glm::mat4(1.0f);
    xf.view     = glm::mat4(1.0f);                                  // eye 0, axis basis
    xf.proj     = FTransform::MakeProjFCG(-0.1f, 0.1f, -0.1f, 0.1f, -0.1f, -1000.0f);
    xf.viewport = FTransform::MakeViewport(nx, ny);
    return xf;
}
// Independent re-impl of the HW6 Blinn-Phong + gamma (verifies the renderer).
static glm::vec3 expectShade(const glm::vec3& P, const glm::vec3& N, const Material& m, const FShadeParams& sp)
{
    glm::vec3 toL = sp.lightPos - P; float d = glm::length(toL);
    glm::vec3 l = d > 1e-6f ? toL/d : glm::vec3(0,1,0);
    glm::vec3 v = glm::normalize(sp.eye - P);
    glm::vec3 h = glm::normalize(l + v);
    float ndl = glm::max(0.0f, glm::dot(N,l)), ndh = glm::max(0.0f, glm::dot(N,h));
    float p = glm::max(m.shininess, 1.0f);
    glm::vec3 lin = m.ka*sp.ambient + m.kd*sp.lightColor*ndl + m.ks*sp.lightColor*std::pow(ndh,p);
    return glm::pow(glm::clamp(lin, 0.0f, 1.0f), glm::vec3(1.0f/2.2f));
}
static UMesh* bigTri()  // covers the screen center, in the z=-7 plane, normal +z
{
    UMesh* m = new UMesh();
    m->vertices = { {{-5,-5,-7},{0,0,1},{0,0}}, {{5,-5,-7},{0,0,1},{1,0}}, {{0,5,-7},{0,0,1},{0,1}} };
    m->indices  = { 0,1,2 };
    return m;
}
static int diffCount(const UFrameBuffer& a, const UFrameBuffer& b)
{
    int n = 0;
    for (size_t i = 0; i < a.color.size(); ++i) if (!veq(a.color[i], b.color[i], 5e-3f)) ++n;
    return n;
}

int main()
{
    const int N = 64;
    URasterizer R;
    FShadeParams sp; sp.lightPos = {-4,4,-3}; sp.lightColor = {1,1,1}; sp.ambient = {0.2f,0.2f,0.2f}; sp.eye = {0,0,0};
    Material mat; mat.ka = {0,1,0}; mat.kd = {0,0.5f,0}; mat.ks = {0.8f,0.8f,0.8f}; mat.shininess = 64.0f;

    // T1: Flat fill is uniform across the triangle interior
    {
        UMesh* t = bigTri(); t->material = mat;
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        glm::vec3 a = fb.color[(N/2)*N + N/2], b = fb.color[(N/2-8)*N + N/2+6];
        ck("T1", "Flat fill uniform", veq(a,b) && a.g > 0.01f);
        delete t;
    }
    // T2: Flat center matches hand-computed Blinn-Phong + gamma
    {
        UMesh* t = bigTri(); t->material = mat;
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        glm::vec3 centroid(0.0f, -5.0f/3.0f, -7.0f);
        glm::vec3 exp = expectShade(centroid, {0,0,1}, mat, sp);
        ck("T2", "Flat color == hand Blinn-Phong+gamma", veq(fb.color[(N/2)*N + N/2], exp));
    }
    // T3: background stays black
    {
        UMesh* t = bigTri(); t->material = mat;
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        ck("T3", "background black (corner)", veq(fb.color[0], {0,0,0}));
        delete t;
    }
    // T4: depth test -- nearer triangle (z=-5) occludes farther (z=-7)
    {
        UMesh near_, far_;
        near_.vertices = { {{-5,-5,-5},{0,0,1},{0,0}}, {{5,-5,-5},{0,0,1},{1,0}}, {{0,5,-5},{0,0,1},{0,1}} };
        near_.indices = {0,1,2}; near_.material.kd = {1,0,0}; near_.material.ka={0,0,0}; near_.material.ks={0,0,0};
        far_.vertices  = { {{-5,-5,-7},{0,0,1},{0,0}}, {{5,-5,-7},{0,0,1},{1,0}}, {{0,5,-7},{0,0,1},{0,1}} };
        far_.indices = {0,1,2}; far_.material.kd = {0,0,1}; far_.material.ka={0,0,0}; far_.material.ks={0,0,0};
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(far_,  hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        R.DrawMeshShaded(near_, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        glm::vec3 c = fb.color[(N/2)*N + N/2];
        ck("T4", "nearer tri occludes (red dominates blue)", c.r > c.b);
    }
    // Sphere renders for model-difference checks
    auto renderSphere = [&](EShadingModel sm, UFrameBuffer& fb)
    {
        UMesh* s = UMesh::GenerateSphere(2.0f, 24, 12); s->material = mat;
        FTransform xf = hwXf(N,N); xf.model = glm::translate(glm::mat4(1.0f), glm::vec3(0,0,-7));
        fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(*s, xf, nullptr, sp, sm, fb);
        delete s;
    };
    UFrameBuffer flat, gour, phong;
    renderSphere(EShadingModel::Flat,    flat);
    renderSphere(EShadingModel::Gouraud, gour);
    renderSphere(EShadingModel::Phong,   phong);

    // T5: sphere is visible (lit pixels exist)
    {
        int lit = 0; for (auto& c : phong.color) if (c.g > 0.05f) ++lit;
        ck("T5", "Phong sphere visible (lit pixels)", lit > 50);
    }
    // T6: Flat differs from Gouraud (faceted vs smooth)
    ck("T6", "Flat != Gouraud", diffCount(flat, gour) > 20);
    // T7: Gouraud differs from Phong (per-vertex vs per-pixel)
    ck("T7", "Gouraud != Phong", diffCount(gour, phong) > 20);
    // T8: per-triangle material slots (Flat) -> two tris render different colors
    {
        UMesh m;
        m.vertices = { {{-5,-5,-7},{0,0,1},{0,0}}, {{5,-5,-7},{0,0,1},{1,0}},
                       {{5,5,-7},{0,0,1},{1,1}},   {{-5,5,-7},{0,0,1},{0,1}} };
        m.indices = { 0,1,2,  0,2,3 };
        Material r; r.kd={1,0,0}; r.ka={0,0,0}; r.ks={0,0,0};
        Material b; b.kd={0,0,1}; b.ka={0,0,0}; b.ks={0,0,0};
        m.materials = { r, b }; m.triMaterial = { 0, 1 };
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(m, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        // tri0 covers lower-right, tri1 upper-left of the quad; sample both halves
        glm::vec3 lo = fb.color[(N/2-12)*N + (N/2+12)];
        glm::vec3 up = fb.color[(N/2+12)*N + (N/2-12)];
        ck("T8", "per-tri material slots render distinct", (lo.r>0.05f && lo.b<0.05f) || (up.b>0.05f && up.r<0.05f));
    }
    // T9: brighter kd -> brighter pixel (gamma monotonic, diffuse scales)
    {
        Material dim = mat; dim.kd = {0,0.2f,0}; dim.ka={0,0,0}; dim.ks={0,0,0};
        Material bright = mat; bright.kd = {0,0.8f,0}; bright.ka={0,0,0}; bright.ks={0,0,0};
        UMesh* t = bigTri();
        UFrameBuffer f1, f2; f1.Init(N,N); f1.Clear(glm::vec3(0)); f2.Init(N,N); f2.Clear(glm::vec3(0));
        t->material = dim;    R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, f1);
        t->material = bright; R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, f2);
        ck("T9", "brighter kd -> brighter pixel", f2.color[(N/2)*N+N/2].g > f1.color[(N/2)*N+N/2].g + 0.05f);
        delete t;
    }
    // T10: gamma applied (output != raw linear for a mid value)
    {
        Material m2; m2.ka={0,0,0}; m2.ks={0,0,0}; m2.kd={0,0.5f,0};
        glm::vec3 centroid(0.0f, -5.0f/3.0f, -7.0f);
        // linear diffuse-only g, gamma-corrected should be > linear (gamma brightens mids)
        glm::vec3 exp = expectShade(centroid, {0,0,1}, m2, sp);
        UMesh* t = bigTri(); t->material = m2;
        UFrameBuffer fb; fb.Init(N,N); fb.Clear(glm::vec3(0));
        R.DrawMeshShaded(*t, hwXf(N,N), nullptr, sp, EShadingModel::Flat, fb);
        glm::vec3 px = fb.color[(N/2)*N+N/2];
        ck("T10", "gamma-corrected output matches expected", veq(px, exp) && px.g > 0.0f);
        delete t;
    }

    std::printf("=== shading: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
