// ---------------------------------------------------------------------------
// world_serialize_test.cpp — GL-free self-test for P6 .world save/load.
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/Serialization -I Engine/World \
//       -I Engine/Core -I Engine/Light -I Engine/Mesh -I Engine/Physics \
//       -I Engine/RayTracing -I Engine/Acceleration \
//       Test/world_serialize_test.cpp Engine/Serialization/FWorldSerializer.cpp \
//       Engine/Serialization/FArchive.cpp Engine/World/UWorld.cpp \
//       Engine/World/AActor.cpp Engine/World/USceneComponent.cpp \
//       Engine/World/ACamera.cpp Engine/Core/UScene.cpp \
//       Engine/Core/UPostProcessFilter.cpp Engine/Mesh/UMesh.cpp \
//       Engine/Mesh/UMeshComponent.cpp Engine/Mesh/Material.cpp \
//       Engine/Acceleration/BVH.cpp Engine/Light/ALight.cpp \
//       Engine/Light/LightComponent.cpp Engine/Light/PointLightComponent.cpp \
//       Engine/Light/EnvironmentLightComponent.cpp Engine/Physics/UPhysicsWorld.cpp \
//       Engine/Physics/UPrimitiveComponent.cpp Engine/Physics/USphereComponent.cpp \
//       Engine/Physics/UBoxComponent.cpp -o world_serialize_test && ./world_serialize_test
// ---------------------------------------------------------------------------
#include "FWorldSerializer.h"
#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "AActor.h"
#include "UMeshComponent.h"
#include "UMesh.h"
#include "ALight.h"
#include "PointLightComponent.h"
#include <cstdio>
#include <cmath>
#include <string>

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

static UWorld* buildWorld()
{
    UWorld* w = new UWorld();
    w->GetScene().shadingModel = 1;                  // Gouraud
    ACamera& cam = w->GetCamera();
    cam.eye = {0,2,8}; cam.yaw = 10; cam.pitch = -5; cam.fov = 60;

    AActor* s = new AActor(); s->name = "Sphere1"; s->SetActorLocation({-2,0,-7});
    UMeshComponent* mc = new UMeshComponent();
    mc->name = "Mesh0"; mc->meshRef = "Sphere 2 32 16";
    mc->hasMaterialOverride = true; mc->materialOverride.kd = {0.2f,0.7f,0.9f};
    s->SetMesh(mc);
    w->Spawn(s);

    ALight* L = new ALight(new PointLightComponent({1,1,1}, {1,1,1}));
    L->name = "Sun"; L->SetActorLocation({-4,4,-3});
    L->lightComp->name = "Light";
    w->Spawn(L);
    return w;
}

int main()
{
    UWorld* w  = buildWorld();
    std::string text = FWorldSerializer::Save(*w);
    UWorld* w2 = FWorldSerializer::Load(text);
    UScene& sc = w2->GetScene();

    ck("T1", "actor count == 2", sc.Actors.size() == 2);

    AActor* s = sc.Actors.size() > 0 ? sc.Actors[0] : nullptr;
    ck("T2", "actor[0] name + location",
       s && s->name == "Sphere1" && veq(s->GetActorLocation(), {-2,0,-7}));

    ck("T3", "mesh resolved from ref (Sphere 868 tris)",
       s && s->mesh && s->mesh->mesh && s->mesh->mesh->triangleCount() == 868 &&
       s->mesh->meshRef == "Sphere 2 32 16");

    ck("T4", "material override kd preserved",
       s && s->mesh && s->mesh->hasMaterialOverride && veq(s->mesh->materialOverride.kd, {0.2f,0.7f,0.9f}));

    ALight* L = sc.Actors.size() > 1 ? dynamic_cast<ALight*>(sc.Actors[1]) : nullptr;
    ck("T5", "light actor: lightComp wired + world pos",
       L && L->name == "Sun" && L->lightComp && veq(L->lightComp->GetWorldLocation(), {-4,4,-3}));

    ACamera& cam = w2->GetCamera();
    ck("T6", "camera fields round-trip",
       veq(cam.eye, {0,2,8}) && std::fabs(cam.yaw-10)<1e-3f && std::fabs(cam.pitch+5)<1e-3f && std::fabs(cam.fov-60)<1e-3f);

    ck("T7", "world ShadingModel round-trip", sc.shadingModel == 1);

    std::string text2 = FWorldSerializer::Save(*w2);
    ck("T8", "save->load->save is identical", text == text2);

    ck("T9", "light component type is PointLight", L && L->lightComp && std::string(L->lightComp->TypeName()) == "PointLight");

    ck("T10", "mesh component name + attached to root",
       s && s->mesh && s->mesh->name == "Mesh0" && s->mesh->attachParent == &s->rootComponent);

    // robustness: garbage text must not crash
    UWorld* bad = FWorldSerializer::Load("garbage\n[Actor]\nType = Nope\n###\n");
    bool robust = (bad != nullptr);
    delete bad;
    ck("T-rob", "garbage load no crash", robust);

    std::printf("=== world_serialize: %d passed, %d failed ===\n", g_pass, g_fail);
    delete w; delete w2;
    return g_fail == 0 ? 0 : 1;
}
