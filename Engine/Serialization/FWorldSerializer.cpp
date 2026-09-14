#include "FWorldSerializer.h"
#include "FArchive.h"

#include "UWorld.h"
#include "UScene.h"
#include "ACamera.h"
#include "AActor.h"
#include "USceneComponent.h"
#include "UActorComponent.h"
#include "../Script/UScriptComponent.h"
#include "UMeshComponent.h"
#include "ALight.h"
#include "LightComponent.h"
#include "UPhysicsWorld.h"
#include "UPrimitiveComponent.h"
#include "USphereComponent.h"
#include "UBoxComponent.h"

#include <sstream>
#include <fstream>
#include <vector>
#include <utility>
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>

namespace
{
    FWorldSerializer::FWarningSink g_warningSink;

    void Warn(const std::string& message)
    {
        if (g_warningSink) g_warningSink(message);
        else std::cerr << "[World] warning: " << message << '\n';
    }

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool ParseBool(const std::string& text, bool& value)
    {
        const std::string v = lower(trim(text));
        if (v == "1" || v == "true" || v == "yes" || v == "on") { value = true; return true; }
        if (v == "0" || v == "false" || v == "no" || v == "off") { value = false; return true; }
        return false;
    }

    const char* BackendName(ERayTracingBackend backend)
    {
        switch (backend)
        {
            case ERayTracingBackend::Auto:           return "Auto";
            case ERayTracingBackend::CompatibleGL33: return "Compatible";
            case ERayTracingBackend::ComputeGL43:    return "Compute";
        }
        return "Auto";
    }

    bool ParseBackend(const std::string& text, ERayTracingBackend& backend)
    {
        const std::string v = lower(trim(text));
        if (v == "auto")       { backend = ERayTracingBackend::Auto; return true; }
        if (v == "compatible") { backend = ERayTracingBackend::CompatibleGL33; return true; }
        if (v == "compute")    { backend = ERayTracingBackend::ComputeGL43; return true; }
        return false;
    }

    bool ApplyLegacyMode(int mode, FRenderFeatures& features)
    {
        if (mode < 0 || mode > 2) return false;
        features.hardwareRaster = true;
        features.rayTracing = mode == 1 || mode == 2;
        features.rayTracedShadows = true;
        features.rayTracedGI = true;
        features.rayTracedReflections = true;
        features.rayTracedTranslucency = true;
        features.rayTracingBackend = ERayTracingBackend::Auto;
        return true;
    }

    bool ParseLegacyMode(const std::string& text, int& mode)
    {
        const std::string token = trim(text);
        if (token.empty()) return false;
        try
        {
            size_t consumed = 0;
            const int parsed = std::stoi(token, &consumed);
            if (consumed != token.size() || parsed < 0 || parsed > 2) return false;
            mode = parsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

std::string FWorldSerializer::Save(UWorld& world)
{
    UScene&  sc  = world.GetScene();
    ACamera& cam = world.GetCamera();

    sc.renderFeatures.hardwareRaster = true;
    std::string out = "WorldFormat = 3\n\n";

    out += "[World]\n";
    {
        FSaveArchive a;
        int sm = sc.shadingModel; a.Field("ShadingModel", sm);
        std::string sky = sc.skyHDRI; a.Field("SkyHDRI", sky);
        UPhysicsWorld& phys = world.GetPhysics();
        int gravEnabled = phys.enableFloor ? 1 : 0; a.Field("FloorEnabled", gravEnabled);
        float fy = phys.floorY; a.Field("FloorY", fy);
        glm::vec3 g = phys.gravity; a.Field("Gravity", g);
        out += a.str();
    }
    out += "\n";

    out += "[RenderFeatures]\n";
    {
        FSaveArchive a;
        bool hardware = true; a.Field("HardwareRaster", hardware);
        bool rayTracing = sc.renderFeatures.rayTracing; a.Field("RayTracing", rayTracing);
        bool shadows = sc.renderFeatures.rayTracedShadows; a.Field("RayTracedShadows", shadows);
        bool gi = sc.renderFeatures.rayTracedGI; a.Field("RayTracedGI", gi);
        bool reflections = sc.renderFeatures.rayTracedReflections; a.Field("RayTracedReflections", reflections);
        bool translucency = sc.renderFeatures.rayTracedTranslucency; a.Field("RayTracedTranslucency", translucency);
        std::string backend = BackendName(sc.renderFeatures.rayTracingBackend); a.Field("RayTracingBackend", backend);
        out += a.str();
    }
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
            a.Field("Type", t); actor->Serialize(a);
            // Parent actor name (scene-graph hierarchy); empty = world root.
            std::string parentName;
            if (actor->rootComponent.attachParent && actor->rootComponent.attachParent->GetOwner())
                parentName = actor->rootComponent.attachParent->GetOwner()->name;
            a.Field("Parent", parentName);
            out += a.str();
        }
        for (const auto& owned : actor->Components())
        {
            UActorComponent* c = owned.get();
            if (!c || c == actor->physics) continue;  // physics retains its legacy [Collision] block
            out += "  [Component]\n";
            FSaveArchive a; std::string t(c->TypeName());
            a.Field("Type", t); c->Serialize(a); out += a.str();
        }
        if (UPrimitiveComponent* p = actor->physics)   // collision shape + rigid body
        {
            out += "  [Collision]\n";
            FSaveArchive a;
            std::string shape = p->GetShape() == EShape::Box ? "Box" : "Sphere";
            a.Field("Shape", shape);
            if (p->GetShape() == EShape::Box)
            { glm::vec3 he = static_cast<UBoxComponent*>(p)->halfExtents; a.Field("HalfExtents", he); }
            else
            { float r = static_cast<USphereComponent*>(p)->radius; a.Field("Radius", r); }
            a.Field("Mass", p->mass);
            a.Field("Restitution", p->restitution);
            a.Field("Friction", p->friction);
            int grav = p->bAffectedByGravity ? 1 : 0; a.Field("Gravity", grav);
            int sim  = p->bSimulate ? 1 : 0;           a.Field("Simulate", sim);
            glm::vec3 off = p->localOffset;            a.Field("Offset", off);
            out += a.str();
        }
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

UWorld* FWorldSerializer::Load(const std::string& text, const FRenderFeatures& defaults)
{
    // A direct symbol reference forces the concrete script component TU from a
    // static Engine.lib before factory lookup.
    RegisterScriptComponentType();
    UWorld* world = new UWorld();
    UScene& sc = world->GetScene();
    sc.renderFeatures = defaults;
    AActor* curActor = nullptr;
    std::vector<std::pair<AActor*, std::string>> pendingParents;   // (child, parent name)

    int worldFormat = 1;
    const size_t firstSection = text.find('[');
    FLoadArchive top(text.substr(0, firstSection));
    top.Field("WorldFormat", worldFormat);
    bool migratedLegacy = false;

    auto flush = [&](const std::string& hdr, const std::string& body)
    {
        if (hdr.empty()) return;                       // top-level (WorldFormat) -> ignore
        FLoadArchive a(body);

        if (hdr == "World")
        {
            int sm = sc.shadingModel; a.Field("ShadingModel", sm); sc.shadingModel = sm;
            if (worldFormat <= 2 && a.HasField("RenderMode"))
            {
                std::string token; a.Field("RenderMode", token);
                int mode = 0;
                if (ParseLegacyMode(token, mode))
                    migratedLegacy = ApplyLegacyMode(mode, sc.renderFeatures) || migratedLegacy;
            }
            std::string sky = sc.skyHDRI; a.Field("SkyHDRI", sky); sc.skyHDRI = sky;
            UPhysicsWorld& phys = world->GetPhysics();
            int fe = phys.enableFloor ? 1 : 0; a.Field("FloorEnabled", fe); phys.enableFloor = (fe != 0);
            float fy = phys.floorY; a.Field("FloorY", fy); phys.floorY = fy;
            glm::vec3 g = phys.gravity; a.Field("Gravity", g); phys.gravity = g;
        }
        else if (hdr == "RenderFeatures" && worldFormat >= 3)
        {
            auto readBool = [&](const char* key, bool& destination)
            {
                if (!a.HasField(key)) return;
                std::string value; a.Field(key, value);
                bool parsed = false;
                if (ParseBool(value, parsed)) destination = parsed;
            };
            readBool("HardwareRaster", sc.renderFeatures.hardwareRaster);
            readBool("RayTracing", sc.renderFeatures.rayTracing);
            readBool("RayTracedShadows", sc.renderFeatures.rayTracedShadows);
            readBool("RayTracedGI", sc.renderFeatures.rayTracedGI);
            readBool("RayTracedReflections", sc.renderFeatures.rayTracedReflections);
            readBool("RayTracedTranslucency", sc.renderFeatures.rayTracedTranslucency);
            if (a.HasField("RayTracingBackend"))
            {
                std::string value; a.Field("RayTracingBackend", value);
                ERayTracingBackend backend;
                if (ParseBackend(value, backend)) sc.renderFeatures.rayTracingBackend = backend;
                else
                {
                    sc.renderFeatures.rayTracingBackend = ERayTracingBackend::Auto;
                    Warn("unknown RayTracingBackend '" + value + "'; using Auto");
                }
            }
            if (!sc.renderFeatures.hardwareRaster)
            {
                sc.renderFeatures.hardwareRaster = true;
                Warn("HardwareRaster=0 is unsupported; normalized to 1");
            }
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
            std::string parentName; a.Field("Parent", parentName);
            if (!parentName.empty()) pendingParents.push_back({ act, parentName });
            world->Spawn(act);
            curActor = act;
        }
        else if (hdr == "Component")
        {
            if (!curActor) return;
            std::string type = "Scene"; a.Field("Type", type);
            std::unique_ptr<UActorComponent> comp(FComponentFactory::Create(type));
            if (!comp)
            {
                Warn("unknown component type '" + type + "' skipped");
                return;
            }
            try { comp->Serialize(a); }
            catch (const std::exception& error)
            {
                Warn("component type '" + type + "' skipped: " + error.what());
                return;
            }
            if (UMeshComponent* mc = dynamic_cast<UMeshComponent*>(comp.get()))
            {
                try { curActor->SetMesh(mc); }
                catch (...)
                {
                    if (comp->GetOwner() == curActor) comp.release();
                    throw;
                }
                comp.release();
            }
            else if (auto* lc = dynamic_cast<LightComponent*>(comp.get());
                     lc && dynamic_cast<ALight*>(curActor))
            {
                try { static_cast<ALight*>(curActor)->SetLightComponent(lc); }
                catch (...)
                {
                    if (comp->GetOwner() == curActor) comp.release();
                    throw;
                }
                comp.release();
            }
            else
            {
                UActorComponent* raw = comp.get();
                curActor->AdoptComponent(raw);
                comp.release(); // Actor ownership now owns raw.
                if (auto* scene = dynamic_cast<USceneComponent*>(raw))
                    scene->AttachTo(&curActor->rootComponent); // only spatial attachments have a parent
            }
        }
        else if (hdr == "Collision")
        {
            if (!curActor) return;
            std::string shape = "Sphere"; a.Field("Shape", shape);
            UPrimitiveComponent* p = nullptr;
            if (shape == "Box")
            {
                auto* b = new UBoxComponent(curActor);
                glm::vec3 he = b->halfExtents; a.Field("HalfExtents", he); b->halfExtents = he;
                p = b;
            }
            else
            {
                auto* s = new USphereComponent(curActor);
                float r = s->radius; a.Field("Radius", r); s->radius = r;
                p = s;
            }
            a.Field("Mass", p->mass);
            a.Field("Restitution", p->restitution);
            a.Field("Friction", p->friction);
            int grav = p->bAffectedByGravity ? 1 : 0; a.Field("Gravity", grav); p->bAffectedByGravity = (grav != 0);
            int sim  = p->bSimulate ? 1 : 0;           a.Field("Simulate", sim); p->bSimulate = (sim != 0);
            glm::vec3 off = p->localOffset;            a.Field("Offset", off); p->localOffset = off;
            curActor->SetPhysics(p);
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

    if (migratedLegacy)
        Warn("legacy RenderMode migrated to named render features");
    // Wire scene-graph parents by name (relative transforms were saved as-is, so
    // attaching reproduces the original world transforms).
    for (auto& pp : pendingParents)
    {
        AActor* parent = nullptr;
        for (AActor* a : sc.Actors) if (a && a->name == pp.second) { parent = a; break; }
        if (parent && parent != pp.first)
            pp.first->rootComponent.AttachTo(&parent->rootComponent);
    }

    for (AActor* a : sc.Actors) if (a) a->rootComponent.MarkDirty();
    return world;
}

void FWorldSerializer::SetWarningSink(FWarningSink sink)
{
    g_warningSink = std::move(sink);
}

UWorld* FWorldSerializer::LoadFromFile(const char* path, const FRenderFeatures& defaults)
{
    std::ifstream f(path);
    if (!f) return nullptr;
    std::stringstream ss; ss << f.rdbuf();
    return Load(ss.str(), defaults);
}
