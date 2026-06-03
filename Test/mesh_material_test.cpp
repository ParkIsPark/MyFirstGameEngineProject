// ---------------------------------------------------------------------------
// mesh_material_test.cpp — GL-free self-test for P3 (mesh slots / .mesh / Material).
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/Mesh -I Engine/Acceleration \
//       -I Engine/Serialization -I Engine/RayTracing \
//       Test/mesh_material_test.cpp Engine/Mesh/UMesh.cpp Engine/Mesh/Material.cpp \
//       Engine/Acceleration/BVH.cpp Engine/Serialization/FArchive.cpp \
//       -o mesh_material_test && ./mesh_material_test
// ---------------------------------------------------------------------------
#include "UMesh.h"
#include "Material.h"
#include "FArchive.h"
#include <cstdio>
#include <cmath>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static bool veq(const glm::vec3& a, const glm::vec3& b, float e = 1e-3f)
{
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}

// A tiny triangle mesh (one tri) with the given material, used to build parts.
static UMesh* tri(const glm::vec3& kd)
{
    UMesh* m = new UMesh();
    m->vertices = { {{0,0,0},{0,0,1},{0,0}}, {{1,0,0},{0,0,1},{1,0}}, {{0,1,0},{0,0,1},{0,1}} };
    m->indices  = { 0, 1, 2 };
    m->material.kd = kd;
    return m;
}

int main()
{
    const char* TMP = "mesh_material_test.tmp.mesh";

    // T1: GenerateSphere matches the course reference counts
    {
        UMesh* s = UMesh::GenerateSphere(2.0f, 32, 16);
        ck("T1", "GenerateSphere 32x16 -> 450 verts / 868 tris",
           (int)s->vertices.size() == 450 && s->triangleCount() == 868);
        delete s;
    }
    // T2: GenerateCube produces a valid triangle list
    {
        UMesh* c = UMesh::GenerateCube(glm::vec3(1.0f));
        ck("T2", "GenerateCube -> tris>0 && indices%3==0",
           c->triangleCount() > 0 && c->indices.size() % 3 == 0);
        delete c;
    }
    // T3: .mesh round-trip (single material)
    {
        UMesh* s = UMesh::GenerateSphere(1.6f, 16, 8);
        s->material.kd = {0.2f, 0.7f, 0.9f};
        bool saved = s->SaveBinary(TMP);
        UMesh* l = UMesh::LoadBinary(TMP);
        bool ok = saved && l &&
                  l->vertices.size() == s->vertices.size() &&
                  l->indices.size()  == s->indices.size()  &&
                  veq(l->material.kd, s->material.kd);
        ck("T3", ".mesh round-trip (single material)", ok);
        delete s; delete l;
    }
    // T4: .mesh round-trip (multi-material slots)
    {
        UMesh m;
        m.vertices = { {{0,0,0},{0,0,1},{0,0}}, {{1,0,0},{0,0,1},{1,0}},
                       {{0,1,0},{0,0,1},{0,1}}, {{1,1,0},{0,0,1},{1,1}} };
        m.indices  = { 0,1,2,  1,3,2 };           // 2 triangles
        Material a; a.kd = {1,0,0}; Material b; b.kd = {0,0,1};
        m.materials = { a, b }; m.triMaterial = { 0, 1 };
        m.SaveBinary(TMP);
        UMesh* l = UMesh::LoadBinary(TMP);
        bool ok = l && l->materials.size() == 2 && l->triMaterial.size() == 2 &&
                  l->triMaterial[0] == 0 && l->triMaterial[1] == 1 &&
                  veq(l->materials[0].kd, {1,0,0}) && veq(l->materials[1].kd, {0,0,1}) &&
                  veq(l->material.kd, {1,0,0});     // material == slot 0
        ck("T4", ".mesh round-trip (material slots)", ok);
        delete l;
    }
    // T5: MergeWithSlots concatenates parts + per-tri slot indices
    {
        UMesh* p0 = tri({1,0,0});
        UMesh* p1 = tri({0,1,0});
        UMesh* merged = UMesh::MergeWithSlots({ p0, p1 });
        bool ok = merged->vertices.size() == 6 && merged->triangleCount() == 2 &&
                  merged->materials.size() == 2 && merged->triMaterial.size() == 2 &&
                  merged->triMaterial[0] == 0 && merged->triMaterial[1] == 1 &&
                  merged->indices[3] == 3;          // 2nd part indices offset by 3
        ck("T5", "MergeWithSlots merges parts into slots", ok);
        delete p0; delete p1; delete merged;
    }
    // T6: materialForTri resolves slot, falls back to `material`
    {
        UMesh m;
        m.indices = { 0,1,2, 3,4,5 };
        Material a; a.kd = {0.1f,0,0}; Material b; b.kd = {0,0.2f,0};
        m.materials = { a, b }; m.triMaterial = { 1, 0 };
        bool ok = veq(m.materialForTri(0).kd, {0,0.2f,0}) && veq(m.materialForTri(1).kd, {0.1f,0,0});
        UMesh single; single.material.kd = {0.5f,0.5f,0.5f};
        ok = ok && veq(single.materialForTri(0).kd, {0.5f,0.5f,0.5f});   // empty slots -> material
        ck("T6", "materialForTri slot resolution + fallback", ok);
    }
    // T7: Material serialize round-trip (incl diffuse-texture fields)
    {
        Material m; m.kd = {0.3f,0.4f,0.5f}; m.shininess = 48.0f;
        m.diffuseTexPath = "Content/brick.png"; m.wrapMode = EWrapMode::Clamp; m.uvTiling = {2.0f, 3.0f};
        FSaveArchive sa; m.Serialize(sa);
        Material n; FLoadArchive la(sa.str()); n.Serialize(la);
        bool ok = veq(n.kd, m.kd) && std::fabs(n.shininess - 48.0f) < 1e-4f &&
                  n.diffuseTexPath == "Content/brick.png" && n.wrapMode == EWrapMode::Clamp &&
                  std::fabs(n.uvTiling.x - 2.0f) < 1e-4f && std::fabs(n.uvTiling.y - 3.0f) < 1e-4f;
        ck("T7", "Material round-trip (path/wrap/tiling)", ok);
    }
    // T8: SampleDiffuse with no texture returns kd
    {
        Material m; m.kd = {0.7f, 0.2f, 0.1f};
        ck("T8", "SampleDiffuse (no texture) == kd", veq(m.SampleDiffuse({0.5f,0.5f}), {0.7f,0.2f,0.1f}));
    }
    // T9: SampleDiffuse reads texData (sRGB->linear). 1x1 white -> ~1.0 linear.
    {
        Material m; m.texWidth = 1; m.texHeight = 1; m.texChannels = 3;
        m.texData = { 255, 255, 255 };
        glm::vec3 c = m.SampleDiffuse({0.5f, 0.5f});
        ck("T9", "SampleDiffuse 1x1 white -> linear ~1", veq(c, {1,1,1}, 1e-2f));
    }
    // T10: wrap repeat vs clamp differ for uv>1 (2x1: black|white)
    {
        Material m; m.texWidth = 2; m.texHeight = 1; m.texChannels = 3;
        m.texData = { 0,0,0,  255,255,255 };       // x=0 black, x=1 white
        m.wrapMode = EWrapMode::Repeat; glm::vec3 r = m.SampleDiffuse({1.5f, 0.0f}); // -> u 0.5 -> mid
        m.wrapMode = EWrapMode::Clamp;  glm::vec3 c = m.SampleDiffuse({1.5f, 0.0f}); // -> u 1.0 -> white
        ck("T10", "wrap repeat<clamp for uv>1", r.x < c.x - 0.1f);
    }

    std::remove(TMP);
    std::printf("=== mesh_material: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
