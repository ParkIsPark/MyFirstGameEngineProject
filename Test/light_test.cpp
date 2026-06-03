// ---------------------------------------------------------------------------
// light_test.cpp — GL-free self-test for P5 light componentization.
//
// Verifies LightComponent is a USceneComponent (transform-driven position,
// serializable, factory-constructible) and the time-of-day preset.
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/Light -I Engine/World -I Engine/Mesh \
//       -I Engine/Serialization \
//       Test/light_test.cpp Engine/Light/PointLight.cpp Engine/Light/EnvironmentLight.cpp \
//       Engine/Light/LightComponent.cpp Engine/Light/ALight.cpp Engine/World/AActor.cpp \
//       Engine/World/USceneComponent.cpp Engine/Serialization/FArchive.cpp \
//       -o light_test && ./light_test
// ---------------------------------------------------------------------------
#include "PointLight.h"
#include "EnvironmentLight.h"
#include "ALight.h"
#include "FArchive.h"
#include <cstdio>
#include <cmath>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static bool veq(const glm::vec3& a, const glm::vec3& b, float e = 1e-3f)
{
    return std::fabs(a.x-b.x)<e && std::fabs(a.y-b.y)<e && std::fabs(a.z-b.z)<e;
}

int main()
{
    // T1: light world position comes from the actor transform
    {
        ALight l(new PointLight(glm::vec3(1), glm::vec3(1)));
        l.SetActorLocation({5, 5, -3});
        ck("T1", "PointLight world pos == actor location", veq(l.lightComp->GetWorldLocation(), {5,5,-3}));
    }
    // T2: lightComp is attached under the actor's root
    {
        ALight l(new PointLight(glm::vec3(1), glm::vec3(1)));
        ck("T2", "lightComp attached to root", l.lightComp->attachParent == &l.rootComponent);
    }
    // T3: PointLight serialize round-trip (color/intensity + transform)
    {
        PointLight p; p.LightColor = {1,0.5f,0.2f}; p.LightIntensity = {2,2,2};
        p.SetRelativeLocation({4,-1,7});
        FSaveArchive sa; p.Serialize(sa);
        PointLight q; FLoadArchive la(sa.str()); q.Serialize(la);
        ck("T3", "PointLight round-trip",
           veq(q.LightColor, {1,0.5f,0.2f}) && veq(q.LightIntensity, {2,2,2}) && veq(q.relLocation, {4,-1,7}));
    }
    // T4: EnvironmentLight serialize round-trip (sky params)
    {
        EnvironmentLight e; e.horizonColor = {0.9f,0.5f,0.2f}; e.zenithColor = {0.3f,0.5f,0.9f}; e.skyExp = 0.7f;
        FSaveArchive sa; e.Serialize(sa);
        EnvironmentLight f; FLoadArchive la(sa.str()); f.Serialize(la);
        ck("T4", "EnvironmentLight round-trip",
           veq(f.horizonColor, {0.9f,0.5f,0.2f}) && veq(f.zenithColor, {0.3f,0.5f,0.9f}) && std::fabs(f.skyExp-0.7f)<1e-4f);
    }
    // T5/T6: factory create by TypeName
    {
        USceneComponent* p = FComponentFactory::Create("PointLight");
        ck("T5", "Create(\"PointLight\")", p && std::string(p->TypeName()) == "PointLight"); delete p;
        USceneComponent* e = FComponentFactory::Create("EnvLight");
        ck("T6", "Create(\"EnvLight\")", e && std::string(e->TypeName()) == "EnvLight"); delete e;
    }
    // T7: noon preset (tod=+10)
    {
        EnvironmentLight e; EnvironmentLight::applyTimeOfDay(e, 10.0f);
        ck("T7", "applyTimeOfDay noon", veq(e.zenithColor, {0.25f,0.55f,1.0f}) && e.LightIntensity.x > 0.9f);
    }
    // T8: midnight preset (tod=-10) -> ~no light
    {
        EnvironmentLight e; EnvironmentLight::applyTimeOfDay(e, -10.0f);
        ck("T8", "applyTimeOfDay midnight intensity ~0", e.LightIntensity.x < 0.01f);
    }
    // T9: LightComponent IS-A USceneComponent (upcast + type tag)
    {
        PointLight p;
        USceneComponent* sc = &p;          // compiles only if LightComponent : USceneComponent
        ck("T9", "PointLight is-a USceneComponent", std::string(sc->TypeName()) == "PointLight");
    }
    // T10: moving the actor moves the light
    {
        ALight l(new PointLight(glm::vec3(1), glm::vec3(1)));
        l.SetActorLocation({1,2,3});
        glm::vec3 a = l.lightComp->GetWorldLocation();
        l.SetActorLocation({4,5,6});
        glm::vec3 b = l.lightComp->GetWorldLocation();
        ck("T10", "light follows actor move", veq(a,{1,2,3}) && veq(b,{4,5,6}));
    }

    std::printf("=== light: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
