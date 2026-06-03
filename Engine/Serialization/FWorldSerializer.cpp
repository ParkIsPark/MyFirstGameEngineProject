#include "FWorldSerializer.h"
#include "FArchive.h"

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "AActor.h"
#include "USceneComponent.h"
#include "UMeshComponent.h"
#include "ALight.h"
#include "LightComponent.h"

#include <sstream>
#include <fstream>

namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
}

std::string FWorldSerializer::Save(UWorld& world)
{
    UScene&  sc  = world.GetScene();
    ACamera& cam = world.GetCamera();

    std::string out = "WorldFormat = 1\n\n";

    out += "[World]\n";
    { FSaveArchive a; int sm = sc.shadingModel; a.Field("ShadingModel", sm); out += a.str(); }
    out += "\n";

    out += "[Camera]\n";
    {
        FSaveArchive a;
        glm::vec3 p = cam.eye; a.Field("Position", p);
        float y = cam.yaw, pi = cam.pitch, f = cam.fov;
        a.Field("Yaw", y); a.Field("Pitch", pi); a.Field("Fov", f);
        out += a.str();
    }
    out += "\n";

    for (AActor* actor : sc.Actors)
    {
        if (!actor) continue;
        out += "[Actor]\n";
        {
            FSaveArchive a; std::string t = actor->TypeName();
            a.Field("Type", t); actor->Serialize(a); out += a.str();
        }
        for (USceneComponent* c : actor->rootComponent.children)
        {
            out += "  [Component]\n";
            FSaveArchive a; std::string t = c->TypeName();
            a.Field("Type", t); c->Serialize(a); out += a.str();
        }
        out += "\n";
    }
    return out;
}

bool FWorldSerializer::SaveToFile(UWorld& world, const char* path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << Save(world);
    return (bool)f;
}

UWorld* FWorldSerializer::Load(const std::string& text)
{
    UWorld* world = new UWorld();
    UScene& sc = world->GetScene();
    AActor* curActor = nullptr;

    auto flush = [&](const std::string& hdr, const std::string& body)
    {
        if (hdr.empty()) return;                       // top-level (WorldFormat) -> ignore
        FLoadArchive a(body);

        if (hdr == "World")
        {
            int sm = sc.shadingModel; a.Field("ShadingModel", sm); sc.shadingModel = sm;
        }
        else if (hdr == "Camera")
        {
            ACamera& cam = world->GetCamera();
            glm::vec3 p = cam.eye; a.Field("Position", p); cam.eye = p;
            float y = cam.yaw, pi = cam.pitch, f = cam.fov;
            a.Field("Yaw", y); a.Field("Pitch", pi); a.Field("Fov", f);
            cam.yaw = y; cam.pitch = pi; cam.fov = f;
            cam.SetOrientation(y, pi);                  // rebuild u/v/w (no aspect needed)
        }
        else if (hdr == "Actor")
        {
            std::string type = "Actor"; a.Field("Type", type);
            AActor* act = FActorFactory::Create(type);
            if (!act) act = new AActor();
            act->Serialize(a);
            world->Spawn(act);
            curActor = act;
        }
        else if (hdr == "Component")
        {
            if (!curActor) return;
            std::string type = "Scene"; a.Field("Type", type);
            USceneComponent* comp = FComponentFactory::Create(type);
            if (!comp) return;
            comp->Serialize(a);
            comp->owner = curActor;
            comp->AttachTo(&curActor->rootComponent);   // flat: attach under root
            if (UMeshComponent* mc = dynamic_cast<UMeshComponent*>(comp))
                curActor->mesh = mc;
            if (LightComponent* lc = dynamic_cast<LightComponent*>(comp))
                if (ALight* al = dynamic_cast<ALight*>(curActor))
                    al->lightComp = lc;
        }
    };

    std::istringstream in(text);
    std::string line, header, body;
    while (std::getline(in, line))
    {
        const std::string t = trim(line);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
        {
            flush(header, body);
            header = t.substr(1, t.size() - 2);
            body.clear();
        }
        else
        {
            body += line; body += "\n";
        }
    }
    flush(header, body);

    for (AActor* a : sc.Actors) if (a) a->rootComponent.MarkDirty();
    return world;
}

UWorld* FWorldSerializer::LoadFromFile(const char* path)
{
    std::ifstream f(path);
    if (!f) return nullptr;
    std::stringstream ss; ss << f.rdbuf();
    return Load(ss.str());
}
