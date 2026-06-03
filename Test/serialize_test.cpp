// ---------------------------------------------------------------------------
// serialize_test.cpp — GL-free self-test for the P2 serialization core.
//
// Verifies FArchive Save/Load round-trips (Material, USceneComponent, AActor),
// the TypeName/Serialize contract, and the TypeName->instance factories.
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/World -I Engine/Mesh \
//       -I Engine/Serialization \
//       Test/serialize_test.cpp Engine/Serialization/FArchive.cpp \
//       Engine/Mesh/Material.cpp Engine/World/USceneComponent.cpp \
//       Engine/World/AActor.cpp -o serialize_test && ./serialize_test
// ---------------------------------------------------------------------------
#include "FArchive.h"
#include "Material.h"
#include "USceneComponent.h"
#include "AActor.h"
#include <cstdio>
#include <cmath>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static bool veq(const glm::vec3& a, const glm::vec3& b, float e = 1e-4f)
{
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}

int main()
{
    // T1: Material round-trip
    {
        Material m;
        m.kd = {0.8f, 0.3f, 0.3f}; m.ks = {0.4f, 0.4f, 0.4f}; m.ka = {0.0f, 1.0f, 0.0f};
        m.shininess = 64.0f; m.km = {0.1f, 0.1f, 0.1f}; m.emissive = {0.2f, 0.0f, 0.0f};
        FSaveArchive sa; m.Serialize(sa);
        Material n; FLoadArchive la(sa.str()); n.Serialize(la);
        ck("T1", "Material round-trip",
           veq(n.kd, m.kd) && veq(n.ks, m.ks) && veq(n.ka, m.ka) &&
           std::fabs(n.shininess - 64.0f) < 1e-4f && veq(n.km, m.km) && veq(n.emissive, m.emissive));
    }
    // T2: USceneComponent round-trip (name + transform)
    {
        USceneComponent a;
        a.name = "Comp"; a.SetRelativeLocation({1, 2, 3});
        a.SetRelativeRotation({10, 20, 30}); a.SetRelativeScale({2, 2, 2});
        FSaveArchive sa; a.Serialize(sa);
        USceneComponent b; FLoadArchive la(sa.str()); b.Serialize(la);
        ck("T2", "USceneComponent round-trip",
           b.name == "Comp" && veq(b.relLocation, {1, 2, 3}) &&
           veq(b.relRotation, {10, 20, 30}) && veq(b.relScale, {2, 2, 2}));
    }
    // T3: AActor round-trip (name + transform via accessors)
    {
        AActor a; a.name = "Hero";
        a.SetActorLocation({-2, 0, -7}); a.SetActorRotation({0, 45, 0}); a.SetActorScale({1.5f, 1.5f, 1.5f});
        FSaveArchive sa; a.Serialize(sa);
        AActor b; FLoadArchive la(sa.str()); b.Serialize(la);
        ck("T3", "AActor round-trip",
           b.name == "Hero" && veq(b.GetActorLocation(), {-2, 0, -7}) &&
           veq(b.GetActorRotation(), {0, 45, 0}) && veq(b.GetActorScale(), {1.5f, 1.5f, 1.5f}));
    }
    // T4: component factory creates by TypeName
    {
        USceneComponent* c = FComponentFactory::Create("Scene");
        bool ok = c && std::string(c->TypeName()) == "Scene";
        delete c;
        ck("T4", "FComponentFactory Create(\"Scene\")", ok);
    }
    // T5: actor factory creates by TypeName
    {
        AActor* a = FActorFactory::Create("Actor");
        bool ok = a && std::string(a->TypeName()) == "Actor";
        delete a;
        ck("T5", "FActorFactory Create(\"Actor\")", ok);
    }
    // T6: unknown type -> nullptr (robust)
    {
        ck("T6", "Create(unknown) -> nullptr",
           FComponentFactory::Create("Nope") == nullptr && FActorFactory::Create("Nope") == nullptr);
    }
    // T7: FLoadArchive robustness -- garbage block, comments, no crash
    {
        FLoadArchive la("# comment\n   \nrandom line without equals\nName = OK\n");
        std::string s = "default"; glm::vec3 v(9, 9, 9);
        la.Field("Name", s); la.Field("Missing", v);
        ck("T7", "robust parse (garbage skipped, missing kept)", s == "OK" && veq(v, {9, 9, 9}));
    }
    // T8: vec3 parse
    {
        FLoadArchive la("P = 1 2 3\n");
        glm::vec3 v(0); la.Field("P", v);
        ck("T8", "vec3 parse '1 2 3'", veq(v, {1, 2, 3}));
    }
    // T9: bool round-trip
    {
        bool b = true; FSaveArchive sa; sa.Field("flag", b);
        bool c = false; FLoadArchive la(sa.str()); la.Field("flag", c);
        ck("T9", "bool round-trip", c == true);
    }
    // T10: missing key leaves value untouched
    {
        FLoadArchive la("Other = 5\n");
        float f = 42.0f; la.Field("Absent", f);
        ck("T10", "missing key keeps default", std::fabs(f - 42.0f) < 1e-6f);
    }

    std::printf("=== serialize: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
