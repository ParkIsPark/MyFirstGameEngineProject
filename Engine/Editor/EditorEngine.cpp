#include "EditorEngine.h"

#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "UMesh.h"
#include "FTransform.h"
#include "UMeshComponent.h"
#include "AActor.h"
#include "ACamera.h"
#include "UScene.h"
#include "URay.h"
#include "UPrimitiveComponent.h"
#include "USphereComponent.h"
#include "UBoxComponent.h"
#include "UPhysicsWorld.h"
#include "ALight.h"
#include "PointLightComponent.h"
#include "EnvironmentLightComponent.h"
#include "LightComponent.h"
#include "FWorldSerializer.h"
#include "FRenderShowFlag.h"
#include "UObjImporter.h"
#include "UFbxImporter.h"
#include "FFileDialog.h"
#include "FProcess.h"

#include <cstdio>
#include <cctype>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include "stb_image.h"          // declaration only; impl lives in USkyHDRI.cpp

namespace {
    const char* kModes[]   = { "Rasterizer", "GPU RT", "Hybrid" };
    const char* kShading[] = { "Flat", "Gouraud", "Phong" };

    // Load an image file into a Material's CPU diffuse texture (used by the CPU
    // raster's SampleDiffuse). stb impl is in USkyHDRI.cpp.
    bool LoadMaterialTexture(Material& m, const std::string& path)
    {
        int w = 0, h = 0, n = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* d = stbi_load(path.c_str(), &w, &h, &n, 0);
        if (!d) { std::printf("[Editor] texture load failed: %s\n", path.c_str()); return false; }
        m.texData.assign(d, d + (size_t)w * h * n);
        m.texWidth = w; m.texHeight = h; m.texChannels = n;
        m.diffuseTexPath = path;
        stbi_image_free(d);
        std::printf("[Editor] texture %s (%dx%d, %dch)\n", path.c_str(), w, h, n);
        return true;
    }
}

EditorEngine::~EditorEngine()
{
    if (imguiReady_)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (pieWorld_) { delete pieWorld_; pieWorld_ = nullptr; }
    for (UWorld* w : undoStack_) delete w;
    for (UWorld* w : redoStack_) delete w;
    delete editorWorld_;
    for (UMesh* m : meshAssets_) delete m;   // UScene dtor deletes the actors
}

// ---------------------------------------------------------------------------
void EditorEngine::BuildEditorWorld()
{
    UMesh* sphere = UMesh::GenerateSphere(1.6f, 28, 14);
    UMesh* cube   = UMesh::GenerateCube(glm::vec3(1.4f));
    meshAssets_.push_back(sphere);
    meshAssets_.push_back(cube);

    auto spawnMesh = [&](const char* name, UMesh* mesh, const char* meshRef, glm::vec3 pos, glm::vec3 kd)
    {
        AActor* a = new AActor();
        a->name = name;                    // identity for serialization / clone / undo
        a->SetActorLocation(pos);
        UMeshComponent* mc = new UMeshComponent();
        mc->mesh = mesh;
        mc->meshRef = meshRef;             // descriptor so .world save/load round-trips
        mc->hasMaterialOverride = true;
        mc->materialOverride.kd        = kd;
        mc->materialOverride.ks        = glm::vec3(0.4f);
        mc->materialOverride.shininess = 32.0f;
        a->SetMesh(mc);
        editorWorld_->Spawn(a);
        actorNames_.push_back(name);
    };
    auto spawnEmpty = [&](const char* name, glm::vec3 pos)
    {
        AActor* a = new AActor();
        a->name = name;
        a->SetActorLocation(pos);
        editorWorld_->Spawn(a);
        actorNames_.push_back(name);
    };

    spawnMesh("Sphere_Ball", sphere, "Sphere 1.6 28 14", glm::vec3(-2.6f, 2.5f, -9.0f), glm::vec3(0.45f, 0.55f, 0.90f));
    spawnMesh("Cube_Box",    cube,   "Cube 1.4 1.4 1.4", glm::vec3( 2.6f, 0.0f, -9.0f), glm::vec3(0.90f, 0.55f, 0.28f));

    // A real light actor (ALight owns a LightComponent) -- inspectable in Details.
    {
        ALight* light = new ALight(new PointLightComponent(glm::vec3(1.0f), glm::vec3(1.0f)));
        light->name = "PointLight";
        light->SetActorLocation(glm::vec3(5.0f, 5.0f, -3.0f));
        editorWorld_->Spawn(light);
        actorNames_.push_back("PointLight");
    }
    spawnEmpty("PlayerStart", glm::vec3(0.0f, -1.0f, -6.0f));

    // Ball: dynamic sphere collider (falls under gravity in PIE).
    {
        AActor* ball = editorWorld_->GetScene().Actors[0];
        USphereComponent* sc = new USphereComponent(ball);
        sc->radius = 1.6f; sc->mass = 1.0f; sc->restitution = 0.4f;
        ball->SetPhysics(sc);
    }
    // Cube: static box collider (mass 0) -- inspectable, doesn't move.
    {
        AActor* box = editorWorld_->GetScene().Actors[1];
        UBoxComponent* bc = new UBoxComponent(box);
        bc->halfExtents = glm::vec3(1.4f); bc->mass = 0.0f;
        box->SetPhysics(bc);
    }

    selected_ = 0;
}

void EditorEngine::SaveWorld()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(contentDir_, ec);
    const std::string path = contentDir_ + "/" + worldName_ + ".world";
    FWorldSerializer::SaveToFile(*editorWorld_, path.c_str());
    content_.clear();          // refresh so the new .world shows in the browser
    ScanContent();
    std::printf("[Editor] Saved %s\n", path.c_str());
}

void EditorEngine::RebuildActorNames()
{
    actorNames_.clear();
    for (AActor* a : editorWorld_->GetScene().Actors)
    {
        std::string n = a->name;
        if (n.empty()) n = dynamic_cast<ALight*>(a) ? "Light" : (a->mesh ? "Mesh" : "Actor");
        actorNames_.push_back(n);
    }
}

void EditorEngine::SetEditorWorld(UWorld* w, const std::string& name)
{
    if (!w) return;
    if (playing_) OnStop();                  // leave PIE before swapping worlds
    delete editorWorld_;
    editorWorld_ = w;
    worldName_   = name;
    RebuildActorNames();
    selected_    = editorWorld_->GetScene().Actors.empty() ? -1 : 0;
    renderMode_  = editorWorld_->GetScene().renderMode;     // adopt the world's render mode

    // Adopt the loaded world's camera into the editor fly-cam state.
    ACamera& cam = editorWorld_->GetCamera();
    camEye_   = cam.eye;
    camYaw_   = cam.yaw;
    camPitch_ = cam.pitch;

    // Geometry changed -> invalidate the GPU upload caches (RT / Hybrid).
    rtUploaded_ = false; hybridUploaded_ = false;
}

void EditorEngine::ClearHistory()
{
    for (UWorld* w : undoStack_) delete w;
    for (UWorld* w : redoStack_) delete w;
    undoStack_.clear();
    redoStack_.clear();
}

void EditorEngine::PushUndo()
{
    if (!editorWorld_) return;
    undoStack_.push_back(CopyWorld(*editorWorld_));     // lossless in-memory snapshot
    if (undoStack_.size() > 64) { delete undoStack_.front(); undoStack_.erase(undoStack_.begin()); }
    for (UWorld* w : redoStack_) delete w;
    redoStack_.clear();                                 // a new edit forks the timeline
}

// View settings (camera + render/shading mode) are NOT part of the undo history:
// moving the camera or switching render mode must not be reverted by Ctrl+Z, and
// undoing an edit must not jump the camera. Capture them before a snapshot swap
// and restore after.
void EditorEngine::Undo()
{
    if (undoStack_.empty() || !editorWorld_) return;
    const glm::vec3 eye = camEye_; const float yaw = camYaw_, pit = camPitch_;
    const int rm = renderMode_, sm = editorWorld_->GetScene().shadingModel;
    redoStack_.push_back(CopyWorld(*editorWorld_));     // current -> redo
    UWorld* prev = undoStack_.back(); undoStack_.pop_back();
    SetEditorWorld(prev, worldName_);                   // adopts prev (takes ownership)
    camEye_ = eye; camYaw_ = yaw; camPitch_ = pit;
    renderMode_ = rm; editorWorld_->GetScene().renderMode = rm;
    editorWorld_->GetScene().shadingModel = sm;
}

void EditorEngine::Redo()
{
    if (redoStack_.empty() || !editorWorld_) return;
    const glm::vec3 eye = camEye_; const float yaw = camYaw_, pit = camPitch_;
    const int rm = renderMode_, sm = editorWorld_->GetScene().shadingModel;
    undoStack_.push_back(CopyWorld(*editorWorld_));     // current -> undo
    UWorld* next = redoStack_.back(); redoStack_.pop_back();
    SetEditorWorld(next, worldName_);                   // adopts next (takes ownership)
    camEye_ = eye; camYaw_ = yaw; camPitch_ = pit;
    renderMode_ = rm; editorWorld_->GetScene().renderMode = rm;
    editorWorld_->GetScene().shadingModel = sm;
}

void EditorEngine::NewWorld()
{
    SetEditorWorld(new UWorld(), "Untitled");
    ClearHistory();
}

void EditorEngine::LoadWorld(const std::string& path)
{
    UWorld* w = FWorldSerializer::LoadFromFile(path.c_str());
    if (!w) { std::printf("[Editor] Load failed: %s\n", path.c_str()); return; }
    std::string stem = std::filesystem::path(path).stem().string();
    SetEditorWorld(w, stem);

    // Reload CPU diffuse textures from their serialized paths (pixels aren't saved).
    auto reload = [](Material& m) { if (!m.diffuseTexPath.empty() && m.texData.empty()) LoadMaterialTexture(m, m.diffuseTexPath); };
    for (AActor* act : editorWorld_->GetScene().Actors)
        if (UMeshComponent* mc = act->mesh)
        { if (mc->hasMaterialOverride) reload(mc->materialOverride); if (mc->mesh) reload(mc->mesh->material); }

    ClearHistory();                          // a freshly opened world starts clean
    std::printf("[Editor] Loaded %s (%d actors)\n", path.c_str(),
                (int)editorWorld_->GetScene().Actors.size());
}

AActor* EditorEngine::AddActor(const char* type, const std::string& name)
{
    PushUndo();                              // capture pre-add state
    // Spawn ~8 units in front of the editor camera so it lands in view.
    ACamera& cam = editorWorld_->GetCamera();
    glm::vec3 fwd = -cam.w;
    if (glm::length(fwd) < 0.1f) fwd = glm::vec3(0, 0, -1);
    const glm::vec3 pos = cam.eye + glm::normalize(fwd) * 8.0f;

    AActor* a = nullptr;
    std::string t = type;
    if (t == "Light")
    {
        a = new ALight(new PointLightComponent(glm::vec3(1.0f), glm::vec3(1.0f)));
    }
    else if (t == "EnvLight")
    {
        a = new ALight(new EnvironmentLightComponent(glm::vec3(1.0f), glm::vec3(1.0f)));
    }
    else if (t == "Cube" || t == "Sphere" || t == "Plane")
    {
        UMesh* mesh; const char* ref;
        if      (t == "Cube")  { mesh = UMesh::GenerateCube(glm::vec3(1.0f));        ref = "Cube 1 1 1"; }
        else if (t == "Plane") { mesh = UMesh::GeneratePlane(glm::vec2(5.0f));       ref = "Plane 5 5"; }
        else                   { mesh = UMesh::GenerateSphere(1.0f, 28, 14);         ref = "Sphere 1 28 14"; }
        meshAssets_.push_back(mesh);
        a = new AActor();
        UMeshComponent* mc = new UMeshComponent();
        mc->mesh = mesh;
        mc->meshRef = ref;
        mc->hasMaterialOverride = true;
        mc->materialOverride.kd        = glm::vec3(0.7f);
        mc->materialOverride.ks        = glm::vec3(0.4f);
        mc->materialOverride.shininess = 32.0f;
        a->SetMesh(mc);
    }
    else  // "Empty" / "Actor"
    {
        a = new AActor();
    }
    a->name = name;
    a->SetActorLocation(pos);
    editorWorld_->Spawn(a);
    actorNames_.push_back(name);
    selected_ = (int)editorWorld_->GetScene().Actors.size() - 1;
    return a;
}

void EditorEngine::ImportAsset(const std::string& path)
{
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    const std::string stem = fs::path(path).stem().string();

    if (ext == ".world") { LoadWorld(path); return; }

    UMesh* mesh = LoadMeshFile(path);
    if (!mesh) { std::printf("[Editor] Import failed: %s\n", path.c_str()); return; }

    PushUndo();                              // capture pre-import state

    AActor* a = new AActor();
    a->name = stem;
    UMeshComponent* mc = new UMeshComponent();
    mc->mesh = mesh;
    mc->meshRef = path;                       // file path (best-effort round-trip)
    a->SetMesh(mc);

    // Auto-frame: imported meshes keep their raw coordinates, which may be far
    // from the origin and at any scale (a downloaded model can be huge/tiny/
    // off-center -> invisible at a fixed spawn). Compute the mesh AABB and place
    // the actor so the mesh centroid lands ~8 units ahead, scaled to ~3 units.
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (const Vertex& v : mesh->vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
    glm::vec3 center(0.0f); float scale = 1.0f;
    if (!mesh->vertices.empty())
    {
        center = (mn + mx) * 0.5f;
        const float extent = glm::max(mx.x - mn.x, glm::max(mx.y - mn.y, mx.z - mn.z));
        if (extent > 1e-4f) scale = 3.0f / extent;
    }
    ACamera& cam = editorWorld_->GetCamera();
    glm::vec3 fwd = -cam.w; if (glm::length(fwd) < 0.1f) fwd = glm::vec3(0, 0, -1);
    const glm::vec3 spawn = cam.eye + glm::normalize(fwd) * 8.0f;
    a->SetActorScale(glm::vec3(scale));
    a->SetActorLocation(spawn - center * scale);   // centroid -> spawn point

    editorWorld_->Spawn(a);
    actorNames_.push_back(stem);
    selected_ = (int)editorWorld_->GetScene().Actors.size() - 1;
    std::printf("[Editor] Imported %s (%d tris, fit scale %.3g)\n",
                path.c_str(), mesh->triangleCount(), scale);
}

// Load a mesh asset by path/descriptor. Imported meshes (.obj/.fbx/.mesh) are
// owned by meshAssets_; descriptors (Sphere/Cube/Plane) come from the shared
// Resolve cache (not owned here). Returns nullptr on failure (never crashes).
UMesh* EditorEngine::LoadMeshFile(const std::string& path)
{
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);

    UMesh* mesh = nullptr;
    if (ext == ".obj")
    {
        mesh = UObjImporter::Load(path.c_str());
    }
    else if (ext == ".fbx")
    {
        std::vector<UMesh*> parts = UFbxImporter::Load(path.c_str());
        if (!parts.empty())
        {
            mesh = parts[0];
            for (size_t i = 1; i < parts.size(); ++i) delete parts[i];  // keep first
        }
    }
    else if (ext == ".mesh")
    {
        mesh = UMesh::LoadBinary(path.c_str());
    }
    else
    {
        return UMesh::Resolve(path);          // descriptor -> shared cache (not owned)
    }
    if (mesh) meshAssets_.push_back(mesh);    // own imported assets
    return mesh;
}

UWorld* EditorEngine::CopyWorld(UWorld& src, bool resetPhysics)
{
    UWorld* dst = new UWorld();
    dst->GetScene().shadingModel = src.GetScene().shadingModel;
    dst->GetCamera() = src.GetCamera();                 // value copy of all camera fields

    for (AActor* sa : src.GetScene().Actors)
    {
        AActor* da = nullptr;

        // Lights: clone the concrete light component so type/params survive.
        if (ALight* sl = dynamic_cast<ALight*>(sa))
        {
            LightComponent* lc = nullptr;
            if (auto* pl = dynamic_cast<PointLightComponent*>(sl->lightComp))
                lc = new PointLightComponent(pl->LightColor, pl->LightIntensity);
            else if (auto* el = dynamic_cast<EnvironmentLightComponent*>(sl->lightComp))
            {
                auto* e = new EnvironmentLightComponent(el->LightColor, el->LightIntensity);
                e->horizonColor = el->horizonColor; e->zenithColor = el->zenithColor; e->skyExp = el->skyExp;
                lc = e;
            }
            else if (sl->lightComp)
                lc = new LightComponent(sl->lightComp->LightColor, sl->lightComp->LightIntensity);
            da = new ALight(lc);
        }
        else
        {
            da = new AActor();
        }

        da->name = sa->name;
        da->SetActorLocation(sa->GetActorLocation());
        da->SetActorRotation(sa->GetActorRotation());
        da->SetActorScale   (sa->GetActorScale());

        if (sa->mesh)
        {
            UMeshComponent* mc = new UMeshComponent(*sa->mesh); // shares UMesh asset; copies meshRef + override + rel xform
            da->SetMesh(mc);                                    // re-parents under da's root
        }
        if (sa->physics)
        {
            UPrimitiveComponent* p = nullptr;
            if (sa->physics->GetShape() == EShape::Sphere)
            { auto* s = new USphereComponent(da); s->radius = static_cast<USphereComponent*>(sa->physics)->radius; p = s; }
            else if (sa->physics->GetShape() == EShape::Box)
            { auto* b = new UBoxComponent(da); b->halfExtents = static_cast<UBoxComponent*>(sa->physics)->halfExtents; p = b; }
            if (p)
            {
                p->mass = sa->physics->mass; p->restitution = sa->physics->restitution;
                p->friction = sa->physics->friction; p->bAffectedByGravity = sa->physics->bAffectedByGravity;
                p->velocity = resetPhysics ? glm::vec3(0.0f) : sa->physics->velocity;
                da->SetPhysics(p);
            }
        }
        dst->Spawn(da);
    }
    return dst;
}

void EditorEngine::OnPlay()
{
    pieWorld_ = CopyWorld(*editorWorld_, /*resetPhysics=*/true);   // deep copy (UMesh shared)
    // No forced floor: bodies fall freely unless the world enables one (a Plane
    // actor with a Box collider can serve as ground).
    pieWorld_->BeginPlay();
    playing_ = true;
}

void EditorEngine::OnStop()
{
    if (pieWorld_) { pieWorld_->EndPlay(); delete pieWorld_; pieWorld_ = nullptr; }
    playing_ = false;
}

void EditorEngine::LaunchGameProcess()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(contentDir_, ec);
    const std::string tmp = contentDir_ + "/__pie.world";       // standalone snapshot
    FWorldSerializer::SaveToFile(*editorWorld_, tmp.c_str());

    const std::string exe = FProcess::ExecutablePath();
    if (exe.empty()) { std::printf("[Editor] cannot resolve exe path\n"); return; }
    const bool ok = FProcess::LaunchDetached(exe, "--game \"" + tmp + "\"");
    std::printf("[Editor] Launch game (new window): %s\n", ok ? "ok" : "FAILED");
}

void EditorEngine::OnStartup()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    imguiReady_ = true;

    worldRT_.Init();           // GPU ray tracer (PIE: GPU RT mode)
    hybrid_.Init();            // hybrid shadow pass (PIE: Hybrid mode)
    gpuReady_ = true;

    editorWorld_ = new UWorld();
    BuildEditorWorld();
    ScanContent();

    // OS drag-drop of .obj/.fbx/.world files -> import into the editor world.
    glfwSetDropCallback(window_, &EditorEngine::dropTrampoline);
}

void EditorEngine::dropTrampoline(GLFWwindow* win, int count, const char** paths)
{
    auto* self = static_cast<EditorEngine*>(glfwGetWindowUserPointer(win));
    if (!self) return;
    for (int i = 0; i < count; ++i) self->ImportAsset(paths[i]);
}

void EditorEngine::EnsureFBO(int w, int h)
{
    if (w == fboW_ && h == fboH_ && fbo_) return;
    fboW_ = w; fboH_ = h;
    if (!fbo_)      glGenFramebuffers(1, &fbo_);
    if (!fboTex_)   glGenTextures(1, &fboTex_);
    if (!fboDepth_) glGenRenderbuffers(1, &fboDepth_);
    glBindTexture(GL_TEXTURE_2D, fboTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void EditorEngine::RenderWorldGPU(int w, int h, int mode)
{
    UWorld& world  = ActiveWorld();
    UScene& scene  = world.GetScene();
    ACamera& cam   = world.GetCamera();

    // Gather the world's mesh instances (+ per-instance albedo) and every light.
    std::vector<const UMesh*> meshes;
    std::vector<glm::mat4>    models;
    std::vector<glm::vec3>    albedos;
    std::vector<float>        mirrors;
    std::vector<const Material*> mats;
    std::vector<glm::vec3>    lightPos, lightColor;
    for (AActor* a : scene.Actors)
    {
        if (UMeshComponent* mc = a->mesh)
            if (mc->mesh)
            {
                meshes.push_back(mc->mesh);
                models.push_back(mc->GetWorldMatrix());
                const Material& mat = mc->hasMaterialOverride ? mc->materialOverride : mc->mesh->material;
                albedos.push_back(mat.kd);
                mirrors.push_back(glm::max(mat.km.x, glm::max(mat.km.y, mat.km.z)));
                mats.push_back(&mat);
            }
        if (ALight* L = dynamic_cast<ALight*>(a))
            if (PointLightComponent* pl = dynamic_cast<PointLightComponent*>(L->lightComp))
            { lightPos.push_back(pl->GetWorldLocation()); lightColor.push_back(pl->LightColor * pl->LightIntensity); }
    }
    if (lightPos.empty()) { lightPos = { glm::vec3(6, 8, 2) }; lightColor = { glm::vec3(1.0f) }; }

    // Signature of the scene GEOMETRY (mesh identity + world transform + albedo).
    // O(numMeshes), independent of resolution/camera. When it is unchanged the
    // BVH + triangle TBOs from last frame are still valid, so we skip the rebuild.
    size_t geomSig = 1469598103934665603ull;             // FNV-1a 64
    {
        auto mix = [&](const void* p, size_t n) {
            const unsigned char* b = static_cast<const unsigned char*>(p);
            for (size_t i = 0; i < n; ++i) { geomSig ^= b[i]; geomSig *= 1099511628211ull; }
        };
        for (size_t i = 0; i < meshes.size(); ++i)
        { mix(&meshes[i], sizeof(meshes[i])); mix(&models[i], sizeof(glm::mat4)); mix(&albedos[i], sizeof(glm::vec3)); mix(&mirrors[i], sizeof(float));
          size_t ts = mats[i] ? mats[i]->texData.size() : 0; mix(&ts, sizeof(ts)); }
    }

    const unsigned int skyTex = sky_.GetOrLoad(scene.skyHDRI);

    EnsureFBO(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (mode == 1)                                   // GPU RT
    {
        if (!rtUploaded_ || geomSig != rtUploadSig_)   // skip when geometry static
        {
            worldRT_.UploadWorld(meshes, models, albedos, lightPos[0], lightColor[0], mirrors, mats);
            rtUploadSig_ = geomSig; rtUploaded_ = true;
        }
        worldRT_.SetLights(lightPos, lightColor);    // all lights may move without geometry
        worldRT_.SetSky(skyTex);
        worldRT_.RenderFrame(cam, w, h);             // camera/light uniforms each frame
    }
    else                                             // Hybrid: CPU G-buffer + GPU shadow
    {
        gbuf_.Init(w, h); gbuf_.Clear();

        // Per-instance transform (camera-dependent -> rebuilt every frame).
        std::vector<FTransform> xfs(meshes.size());
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            xfs[i].model    = models[i];
            xfs[i].view     = FTransform::MakeView(cam);
            xfs[i].proj     = FTransform::MakeProjFCG(cam.l, cam.r, cam.b, cam.t, -cam.d, -1000.0f);
            xfs[i].viewport = FTransform::MakeViewport(w, h);
        }

        // Shadow-ray geometry (world-space tris + BVH) depends only on geometry,
        // so flatten + upload it only when the signature changes -- not per frame.
        if (!hybridUploaded_ || geomSig != hybridUploadSig_)
        {
            std::vector<glm::vec3> tris;
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const int nt = meshes[i]->triangleCount();
                for (int t = 0; t < nt; ++t)
                    for (int k = 0; k < 3; ++k)
                        tris.push_back(glm::vec3(models[i] * glm::vec4(meshes[i]->vertices[meshes[i]->indices[3*t+k]].position, 1.0f)));
            }
            hybrid_.UploadSceneTriangles(tris);
            hybridUploadSig_ = geomSig; hybridUploaded_ = true;
        }

        // RASTER fills the G-buffer (primary visibility), parallelized over screen
        // tiles. Each tile owns a disjoint pixel rect, so the per-pixel depth test
        // + write never races -- no locks needed. Triangle setup is redundant per
        // tile but cheap next to the pixel work.
        const int TILE = 64;
        const int ntx  = (w + TILE - 1) / TILE;
        const int nty  = (h + TILE - 1) / TILE;
        const int nTiles = ntx * nty;
        pool_.ParallelForChunks(nTiles, [&](int begin, int end)
        {
            for (int tile = begin; tile < end; ++tile)
            {
                const int tx = tile % ntx, ty = tile / ntx;
                const int cx0 = tx * TILE, cy0 = ty * TILE;
                const int cx1 = std::min(cx0 + TILE - 1, w - 1);
                const int cy1 = std::min(cy0 + TILE - 1, h - 1);
                for (size_t i = 0; i < meshes.size(); ++i)
                    rast_.DrawMeshGBuffer(*meshes[i], xfs[i], albedos[i], gbuf_,
                                          cx0, cy0, cx1, cy1, /*countStats=*/false, mats[i]);
            }
        });

        hybrid_.SetSky(skyTex);
        hybrid_.UploadGBuffer(gbuf_);                // camera-dependent -> every frame
        hybrid_.Render(cam, lightPos, lightColor, w, h);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, Width(), Height());
}

void EditorEngine::ScanContent()
{
    namespace fs = std::filesystem;
    const char* dirs[] = { "Content", "bin/Content", "../bin/Content" };
    for (const char* d : dirs)
    {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        contentDir_ = d;
        for (const auto& e : fs::directory_iterator(d, ec))
        {
            if (!e.is_regular_file()) continue;
            std::string name = e.path().filename().string();
            std::string ext  = e.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);

            const char* cat = "Other"; const char* icon = "[?]";
            if      (ext == ".world")                         { cat = "World";   icon = "[W]"; }
            else if (ext == ".obj" || ext == ".fbx")          { cat = "Mesh";    icon = "[M]"; }
            else if (ext == ".png" || ext == ".jpg")          { cat = "Texture"; icon = "[T]"; }
            else if (ext == ".hdr")                           { cat = "HDRI";    icon = "[H]"; }
            content_.push_back({ name, cat, icon });
        }
        break;                                   // first existing dir wins
    }
}

void EditorEngine::Render()
{
    if (!imguiReady_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    if (playing_ && pieWorld_)
        pieWorld_->Tick(ImGui::GetIO().DeltaTime);     // PIE: physics + actor ticks
    DrawUI();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------------------
void EditorEngine::DrawUI()
{
    DrawMenuBar();   // BeginMainMenuBar shrinks the viewport work area below

    // Keyboard shortcuts (editor only; ignore while typing in a text field).
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!playing_ && io.KeyCtrl && !io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveWorld();
        }
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->WorkPos, sz = vp->WorkSize;
    const float toolH = 40.0f, statusH = 24.0f, leftW = 230.0f, rightW = 300.0f, cbH = 168.0f;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(ImVec2(sz.x, toolH));
    if (ImGui::Begin("##Toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar)) DrawToolbar();
    ImGui::End();

    const float by = pos.y + toolH;
    const float bh = sz.y - toolH - statusH;
    const float centerW = sz.x - leftW - rightW;

    ImGui::SetNextWindowPos(ImVec2(pos.x, by)); ImGui::SetNextWindowSize(ImVec2(leftW, bh));
    if (ImGui::Begin("World Outliner", nullptr, fixed)) DrawOutliner();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, by));
    ImGui::SetNextWindowSize(ImVec2(centerW, bh - cbH));
    if (ImGui::Begin("Viewport", nullptr, fixed)) DrawViewport();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, by + bh - cbH));
    ImGui::SetNextWindowSize(ImVec2(centerW, cbH));
    if (ImGui::Begin("Content Browser", nullptr, fixed)) DrawContentBrowser();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + sz.x - rightW, by));
    ImGui::SetNextWindowSize(ImVec2(rightW, bh));
    if (ImGui::Begin("Details", nullptr, fixed)) DrawDetails();
    ImGui::End();

    DrawStatusBar(pos.x, by + bh, sz.x, statusH);

    if (showBuildLog_) DrawBuildLog();
    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

void EditorEngine::DrawBuildLog()
{
    ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Log", &showBuildLog_))
    {
        const char* status = buildMgr_.IsRunning() ? "Building..."
                           : buildMgr_.Done() ? (buildMgr_.Succeeded() ? "Succeeded" : "Failed")
                           : "Idle";
        const ImVec4 col = buildMgr_.IsRunning() ? ImVec4(0.88f, 0.78f, 0.25f, 1)
                         : (buildMgr_.Done() && buildMgr_.Succeeded()) ? ImVec4(0.30f, 0.78f, 0.42f, 1)
                         : (buildMgr_.Done()) ? ImVec4(0.90f, 0.35f, 0.30f, 1)
                                              : ImVec4(0.6f, 0.6f, 0.6f, 1);
        ImGui::TextColored(col, "Status: %s", status);
        ImGui::SameLine();
        ImGui::BeginDisabled(buildMgr_.IsRunning());
        if (ImGui::SmallButton("Rebuild Engine")) buildMgr_.Start("Engine.sln", "Debug", "Engine");
        ImGui::EndDisabled();
        ImGui::Separator();

        ImGui::BeginChild("##buildout", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const std::string& line : buildMgr_.Snapshot())
            ImGui::TextUnformatted(line.c_str());
        if (buildMgr_.IsRunning() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);     // autoscroll while building
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorEngine::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New World")) NewWorld();
        if (ImGui::MenuItem("Save World", "Ctrl+S")) SaveWorld();
        if (ImGui::BeginMenu("Open World"))
        {
            bool any = false;
            for (const ContentEntry& e : content_)
                if (std::string(e.cat) == "World")
                { any = true; if (ImGui::MenuItem(e.name.c_str())) LoadWorld(contentDir_ + "/" + e.name); }
            if (!any) ImGui::TextDisabled("(no .world in Content/)");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(window_, GL_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack_.empty())) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack_.empty())) Redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("World"))
    {
        std::string& sky = ActiveWorld().GetScene().skyHDRI;
        ImGui::TextDisabled("Sky HDRI: %s", sky.empty() ? "(gradient)" : sky.c_str());
        if (ImGui::MenuItem("Set Sky HDRI..."))
        { std::string p = FFileDialog::OpenAsset(); if (!p.empty()) sky = p; }
        if (ImGui::MenuItem("Clear Sky HDRI", nullptr, false, !sky.empty())) sky.clear();
        ImGui::TextDisabled("(visible in GPU RT / Hybrid modes)");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Build"))
    {
        if (ImGui::MenuItem("Build Engine", nullptr, false, !buildMgr_.IsRunning()))
        { buildMgr_.Start("Engine.sln", "Debug", "Engine"); showBuildLog_ = true; }
        if (ImGui::MenuItem("Build Log", nullptr, showBuildLog_)) showBuildLog_ = !showBuildLog_;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) { ImGui::MenuItem("Reset Layout"); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Help"))   { ImGui::MenuItem("About"); ImGui::EndMenu(); }

    const char* title = "MyEngine Editor -- DefaultWorld.world";
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x - 16);
    ImGui::TextDisabled("%s", title);
    ImGui::EndMainMenuBar();
}

void EditorEngine::DrawContentBrowser()
{
    namespace fs = std::filesystem;
    const char* tabs[] = { "All", "World", "Mesh", "Texture" };
    for (int i = 0; i < 4; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(tabs[i], cbFilter_ == i)) cbFilter_ = i; }
    ImGui::SameLine(); if (ImGui::SmallButton("Refresh"))   { content_.clear(); ScanContent(); }
    ImGui::SameLine(); bool importClicked = ImGui::SmallButton("Import...");
    ImGui::SameLine(); ImGui::TextDisabled("  %s/", contentDir_.c_str());
    ImGui::Separator();

    const float cell = 96.0f;
    const int cols = (int)(ImGui::GetContentRegionAvail().x / cell);
    int shown = 0;
    std::string toDelete, renFrom, renTo;
    for (int i = 0; i < (int)content_.size(); ++i)
    {
        const ContentEntry& e = content_[i];
        const std::string cat = e.cat;
        if (cbFilter_ != 0 && cat != tabs[cbFilter_]) continue;
        if (shown % (cols < 1 ? 1 : cols) != 0) ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::BeginGroup();
        ImGui::Button((std::string(e.icon) + "##icon").c_str(), ImVec2(74, 52));

        // Mesh / Texture assets are drag sources -> drop onto a Details slot.
        if ((cat == "Mesh" || cat == "Texture") && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            std::string full = contentDir_ + "/" + e.name;
            ImGui::SetDragDropPayload(cat == "Mesh" ? "ASSET_MESH" : "ASSET_TEX", full.c_str(), full.size() + 1);
            ImGui::Text("%s %s", e.icon, e.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if      (cat == "World") LoadWorld(contentDir_ + "/" + e.name);
            else if (cat == "Mesh")  ImportAsset(contentDir_ + "/" + e.name);
            else if (cat == "HDRI")  editorWorld_->GetScene().skyHDRI = contentDir_ + "/" + e.name;
        }
        if (ImGui::BeginPopupContextItem("ctx"))
        {
            if (cat == "World" && ImGui::MenuItem("Open"))         LoadWorld(contentDir_ + "/" + e.name);
            if (cat == "Mesh"  && ImGui::MenuItem("Add to Scene")) ImportAsset(contentDir_ + "/" + e.name);
            if (cat == "HDRI"  && ImGui::MenuItem("Set as Sky"))   editorWorld_->GetScene().skyHDRI = contentDir_ + "/" + e.name;
            if (ImGui::MenuItem("Rename")) { cbRename_ = i; std::snprintf(cbBuf_, sizeof(cbBuf_), "%s", e.name.c_str()); }
            if (ImGui::MenuItem("Delete")) toDelete = e.name;
            ImGui::EndPopup();
        }

        if (cbRename_ == i)
        {
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputText("##ren", cbBuf_, sizeof(cbBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
            { renFrom = e.name; renTo = cbBuf_; cbRename_ = -1; }
            if (ImGui::IsItemDeactivated()) cbRename_ = -1;
        }
        else
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 80);
            ImGui::TextWrapped("%s", e.name.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        ImGui::PopID();
        ++shown;
    }
    if (shown == 0) ImGui::TextDisabled("(no assets in this filter)");

    if (ImGui::BeginPopupContextWindow("cbWin",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("New World"))         NewWorld();
        if (ImGui::MenuItem("Import OBJ/FBX...")) importClicked = true;
        if (ImGui::MenuItem("Refresh"))           { content_.clear(); ScanContent(); }
        ImGui::EndPopup();
    }

    // Native OS file picker (blocks until the user picks/cancels). Deferred to
    // here so it is never invoked while an ImGui popup is mid-frame.
    if (importClicked)
    {
        std::string p = FFileDialog::OpenAsset();
        if (!p.empty()) ImportAsset(p);
    }

    std::error_code ec;
    if (!toDelete.empty())
    { fs::remove(contentDir_ + "/" + toDelete, ec); content_.clear(); ScanContent(); }
    if (!renFrom.empty() && !renTo.empty() && renFrom != renTo)
    { fs::rename(contentDir_ + "/" + renFrom, contentDir_ + "/" + renTo, ec); content_.clear(); ScanContent(); }
}

void EditorEngine::DrawStatusBar(float x, float y, float w, float h)
{
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    if (ImGui::Begin("##StatusBar", nullptr, f))
    {
        ImGui::TextColored(playing_ ? ImVec4(0.88f, 0.55f, 0.20f, 1) : ImVec4(0.30f, 0.78f, 0.42f, 1),
                           playing_ ? "Play-In-Editor" : "Editor Mode");
        ImGui::SameLine(); ImGui::TextDisabled("|  Selected: %s",
            (selected_ >= 0 && selected_ < (int)actorNames_.size()) ? actorNames_[selected_].c_str() : "(none)");
        ImGui::SameLine(); ImGui::TextDisabled("|  Mode: %s", kModes[renderMode_]);
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        ImGui::TextDisabled("Alt+P Play  *  %.0f FPS", ImGui::GetIO().Framerate);
    }
    ImGui::End();
}

void EditorEngine::DrawToolbar()
{
    if (!playing_) { if (ImGui::Button("|>  Play")) OnPlay(); }
    else           { if (ImGui::Button("[]  Stop")) OnStop(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing_);
    if (ImGui::Button(">>  Play (Window)")) LaunchGameProcess();   // separate process
    ImGui::EndDisabled();

    // Undo / Redo (editor only; disabled when the stack is empty).
    ImGui::SameLine(0, 16);
    ImGui::BeginDisabled(playing_ || undoStack_.empty());
    if (ImGui::Button("Undo")) Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(playing_ || redoStack_.empty());
    if (ImGui::Button("Redo")) Redo();
    ImGui::EndDisabled();

    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Render Mode"); ImGui::SameLine();
    for (int i = 0; i < 3; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(kModes[i], renderMode_ == i)) renderMode_ = i; }
    ActiveWorld().GetScene().renderMode = renderMode_;   // persist into the world (.world save)

    // Shading model (HW6 Q1-Q3) drives the CPU raster preview (editor + PIE raster).
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Shading"); ImGui::SameLine();
    int& sm = ActiveWorld().GetScene().shadingModel;     // 0=Flat 1=Gouraud 2=Phong
    for (int i = 0; i < 3; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(kShading[i], sm == i)) sm = i; }
    ImGui::SameLine(0, 12); ImGui::Checkbox("Depth", &depthView_);

    // Transform gizmo op + space (ImGuizmo, editor only).
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Gizmo"); ImGui::SameLine();
    const char* gops[] = { "Move", "Rotate", "Scale" };
    for (int i = 0; i < 3; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(gops[i], gizmoOp_ == i)) gizmoOp_ = i; }
    ImGui::SameLine(0, 8); ImGui::Checkbox("Local", &gizmoLocal_);

    const float fps = ImGui::GetIO().Framerate;
    ImGui::SameLine(ImGui::GetWindowWidth() - 210);
    ImGui::Text("FPS %.0f  (%.2f ms)", fps, 1000.0f / fps);
    ImGui::SameLine(); ImGui::Checkbox("Demo", &showDemo_);
}

void EditorEngine::DrawOutliner()
{
    auto& actors = ActiveWorld().GetScene().Actors;

    ImGui::BeginDisabled(playing_);                 // structure edits are editor-only
    if (ImGui::Button("+ Add Actor")) ImGui::OpenPopup("AddActorMenu");
    if (ImGui::BeginPopup("AddActorMenu"))
    {
        if (ImGui::MenuItem("Empty Actor")) AddActor("Empty",   "Actor");
        if (ImGui::MenuItem("Cube"))        AddActor("Cube",    "Cube");
        if (ImGui::MenuItem("Sphere"))      AddActor("Sphere",  "Sphere");
        if (ImGui::MenuItem("Plane"))       AddActor("Plane",   "Plane");
        if (ImGui::MenuItem("Point Light")) AddActor("Light",   "PointLight");
        if (ImGui::MenuItem("Env Light"))   AddActor("EnvLight","EnvLight");
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::TextDisabled("%d actors", (int)actors.size());
    ImGui::Separator();

    int toDelete = -1;
    for (int i = 0; i < (int)actors.size(); ++i)
    {
        const char* icon = actors[i]->mesh ? "[M]"
                         : (dynamic_cast<ALight*>(actors[i]) ? "[L]" : "[*]");
        char label[160];
        std::snprintf(label, sizeof(label), "%s %s##act%d", icon, actorNames_[i].c_str(), i);
        if (ImGui::Selectable(label, selected_ == i)) selected_ = i;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FocusActor(i);                       // double-click -> fly camera to it

        if (!playing_ && ImGui::BeginPopupContextItem())
        {
            selected_ = i;
            if (ImGui::IsWindowAppearing())
                std::snprintf(cbBuf_, sizeof(cbBuf_), "%s", actorNames_[i].c_str());
            if (ImGui::InputText("Name", cbBuf_, sizeof(cbBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
            { PushUndo(); actorNames_[i] = cbBuf_; actors[i]->name = cbBuf_; ImGui::CloseCurrentPopup(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) toDelete = i;
            ImGui::EndPopup();
        }
    }

    if (toDelete >= 0)                              // deferred: never mutate mid-iteration
    {
        PushUndo();                                 // capture pre-delete state
        delete actors[toDelete];
        actors.erase(actors.begin() + toDelete);
        actorNames_.erase(actorNames_.begin() + toDelete);
        if (selected_ >= (int)actors.size()) selected_ = (int)actors.size() - 1;
        rtUploaded_ = false; hybridUploaded_ = false;   // geometry changed
    }
}

void EditorEngine::DrawDetails()
{
    auto& actors = ActiveWorld().GetScene().Actors;
    if (selected_ < 0 || selected_ >= (int)actors.size())
    { ImGui::TextDisabled("Select an actor in the World Outliner."); return; }

    AActor* a = actors[selected_];
    UMeshComponent*      mc    = a->mesh;
    UPrimitiveComponent* phys  = a->physics;
    ALight*              light = dynamic_cast<ALight*>(a);

    ImGui::Text("%s", actorNames_[selected_].c_str());
    if (playing_) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.88f, 0.66f, 0.35f, 1), "(PIE read-only)"); }
    ImGui::Separator();

    // ---- Component selector: click a component to inspect it ----
    ImGui::TextDisabled("Components");
    if (ImGui::Selectable("Actor (Root)", detailComp_ == 0)) detailComp_ = 0;
    if (mc    && ImGui::Selectable("Mesh Component", detailComp_ == 1)) detailComp_ = 1;
    if (phys)
    {
        const char* cs = phys->GetShape() == EShape::Sphere ? "Collision (Sphere)"
                       : phys->GetShape() == EShape::Box    ? "Collision (Box)" : "Collision";
        if (ImGui::Selectable(cs, detailComp_ == 2)) detailComp_ = 2;
    }
    if (light && light->lightComp && ImGui::Selectable("Light Component", detailComp_ == 3)) detailComp_ = 3;
    // fall back to Root if the selected component does not exist on this actor
    if ((detailComp_ == 1 && !mc) || (detailComp_ == 2 && !phys) ||
        (detailComp_ == 3 && !(light && light->lightComp))) detailComp_ = 0;
    ImGui::Separator();

    // Snapshot on edit-start (value unchanged that frame -> correct "before" undo).
    auto snap = [&] { if (ImGui::IsItemActivated()) PushUndo(); };
    ImGui::BeginDisabled(playing_);

    // ---- Actor (root) transform ----
    if (detailComp_ == 0)
    {
        ImGui::SeparatorText("Transform (Actor)");
        glm::vec3 loc = a->GetActorLocation();
        if (ImGui::DragFloat3("Position", &loc.x, 0.05f))               a->SetActorLocation(loc);
        snap();
        glm::vec3 rot = a->GetActorRotation();
        if (ImGui::DragFloat3("Rotation", &rot.x, 1.0f))                a->SetActorRotation(rot);
        snap();
        glm::vec3 scl = a->GetActorScale();
        if (ImGui::DragFloat3("Scale",    &scl.x, 0.05f, 0.01f, 100.0f)) a->SetActorScale(scl);
        snap();
    }
    // ---- Mesh component ----
    else if (detailComp_ == 1 && mc)
    {
        ImGui::SeparatorText("Mesh Component");
        const std::string ref = mc->meshRef.empty()
            ? (mc->mesh ? "(in-memory mesh)" : "(none)") : mc->meshRef;
        ImGui::Text("Mesh: %s", ref.c_str());
        if (mc->mesh) ImGui::Text("Triangles: %d", mc->mesh->triangleCount());

        // Procedural primitive tessellation: a sphere's polygon count is its
        // segment counts. Editing regenerates the (cached) mesh via the descriptor.
        {
            std::istringstream iss(mc->meshRef);
            std::string kind; iss >> kind;
            float r = 1.0f; int sw = 32, sh = 16;
            if (kind == "Sphere" && (iss >> r >> sw >> sh))
            {
                ImGui::SeparatorText("Tessellation (Sphere)");
                bool ch = false;
                ch |= ImGui::SliderInt("Segments W", &sw, 3, 128);  if (ImGui::IsItemActivated()) PushUndo();
                ch |= ImGui::SliderInt("Segments H", &sh, 2, 128);  if (ImGui::IsItemActivated()) PushUndo();
                ch |= ImGui::DragFloat("Mesh Radius", &r, 0.02f, 0.05f, 100.0f); if (ImGui::IsItemActivated()) PushUndo();
                if (ch)
                {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "Sphere %g %d %d", r, sw, sh);
                    mc->meshRef = buf;
                    mc->mesh = UMesh::Resolve(buf);            // cached generate/share
                    rtUploaded_ = false; hybridUploaded_ = false;
                }
            }
            else if (kind == "Cube")
                ImGui::TextDisabled("Cube: 12 triangles (fixed)");
        }

        glm::vec3 mloc = mc->relLocation;
        if (ImGui::DragFloat3("Position", &mloc.x, 0.05f)) { mc->relLocation = mloc; mc->MarkDirty(); }
        snap();
        glm::vec3 mrot = mc->relRotation;
        if (ImGui::DragFloat3("Rotation", &mrot.x, 1.0f))  { mc->relRotation = mrot; mc->MarkDirty(); }
        snap();
        glm::vec3 mscl = mc->relScale;
        if (ImGui::DragFloat3("Scale",    &mscl.x, 0.05f, 0.01f, 100.0f)) { mc->relScale = mscl; mc->MarkDirty(); }
        snap();

        ImGui::Button(mc->mesh ? "Replace Mesh  (drop asset / click)"
                               : "Assign Mesh  (drop asset / click)", ImVec2(-1, 0));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_MESH"))
            {
                std::string p((const char*)pl->Data);
                if (UMesh* nm = LoadMeshFile(p))
                { PushUndo(); mc->mesh = nm; mc->meshRef = p; rtUploaded_ = false; hybridUploaded_ = false; }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked())
        {
            std::string p = FFileDialog::OpenAsset();
            if (!p.empty())
                if (UMesh* nm = LoadMeshFile(p))
                { PushUndo(); mc->mesh = nm; mc->meshRef = p; rtUploaded_ = false; hybridUploaded_ = false; }
        }
        if (mc->mesh)
        {
            Material& m = mc->hasMaterialOverride ? mc->materialOverride : mc->mesh->material;
            ImGui::ColorEdit3("Diffuse",   &m.kd.x);       snap();
            ImGui::ColorEdit3("Specular",  &m.ks.x);       snap();
            ImGui::DragFloat ("Shininess", &m.shininess, 1.0f, 0.0f, 256.0f); snap();
            float mir = glm::max(m.km.x, glm::max(m.km.y, m.km.z));
            if (ImGui::SliderFloat("Mirror", &mir, 0.0f, 1.0f)) m.km = glm::vec3(mir);
            snap();
            ImGui::TextDisabled("(Mirror reflection shows in GPU RT mode)");

            // Diffuse texture slot: drag an image from the Content Browser, or
            // click to pick a file. Sampled by the CPU raster (Rasterizer mode).
            const std::string tex = m.diffuseTexPath.empty() ? "(none)" : m.diffuseTexPath;
            ImGui::Text("Texture: %s", tex.c_str());
            ImGui::Button("Set Diffuse Texture  (drop image / click)", ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_TEX"))
                { PushUndo(); LoadMaterialTexture(m, std::string((const char*)pl->Data)); }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemClicked())
            {
                std::string p = FFileDialog::OpenAsset();
                if (!p.empty()) { PushUndo(); LoadMaterialTexture(m, p); }
            }
            if (!m.diffuseTexPath.empty() && ImGui::SmallButton("Clear Texture"))
            { PushUndo(); m.texData.clear(); m.texWidth = m.texHeight = 0; m.diffuseTexPath.clear(); }
        }
    }
    // ---- Collision component: its own transform (offset + size) + rigid body ----
    else if (detailComp_ == 2 && phys)
    {
        ImGui::SeparatorText("Collision Transform");
        glm::vec3 off = phys->localOffset;
        if (ImGui::DragFloat3("Position", &off.x, 0.05f)) phys->localOffset = off;
        snap();
        if (phys->GetShape() == EShape::Sphere)
        { ImGui::DragFloat ("Radius (size)", &static_cast<USphereComponent*>(phys)->radius, 0.05f, 0.01f, 1000.0f); snap(); }
        else if (phys->GetShape() == EShape::Box)
        { ImGui::DragFloat3("Half Extents (size)", &static_cast<UBoxComponent*>(phys)->halfExtents.x, 0.05f, 0.01f, 1000.0f); snap(); }

        ImGui::SeparatorText("Rigid Body");
        if (ImGui::Checkbox("Simulate Physics", &phys->bSimulate)) PushUndo();
        ImGui::SameLine(); ImGui::TextDisabled(phys->IsStatic() ? "(static)" : "(dynamic)");
        ImGui::BeginDisabled(!phys->bSimulate);
        ImGui::DragFloat("Mass",        &phys->mass,        0.1f,  0.0f, 100.0f); snap();
        ImGui::DragFloat("Restitution", &phys->restitution, 0.01f, 0.0f, 1.0f);  snap();
        ImGui::DragFloat("Friction",    &phys->friction,    0.01f, 0.0f, 1.0f);  snap();
        if (ImGui::Checkbox("Affected by Gravity", &phys->bAffectedByGravity)) PushUndo();
        ImGui::EndDisabled();
    }
    // ---- Light component ----
    else if (detailComp_ == 3 && light && light->lightComp)
    {
        LightComponent* lc = light->lightComp;
        ImGui::SeparatorText("Light Component");
        ImGui::ColorEdit3("Light Color", &lc->LightColor.x);                       snap();
        ImGui::DragFloat3("Intensity",   &lc->LightIntensity.x, 0.05f, 0.0f, 50.0f); snap();
        ImGui::TextDisabled("Position = actor transform (Root)");
    }

    // ---- Add Component (available from any view) ----
    ImGui::Spacing();
    if (ImGui::Button("+ Add Component")) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent"))
    {
        if (!a->physics && ImGui::MenuItem("Sphere Collision"))
        { PushUndo(); auto* s = new USphereComponent(a); s->radius = 1.0f; a->SetPhysics(s); detailComp_ = 2; }
        if (!a->physics && ImGui::MenuItem("Box Collision"))
        { PushUndo(); auto* b = new UBoxComponent(a); b->halfExtents = glm::vec3(1.0f); a->SetPhysics(b); detailComp_ = 2; }
        if (a->physics) ImGui::TextDisabled("(already has a collider)");
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
}

void EditorEngine::EnsureViewportTex(int w, int h)
{
    if (w == vpTexW_ && h == vpTexH_ && vpTex_) return;
    vpTexW_ = w; vpTexH_ = h;
    if (!vpTex_) glGenTextures(1, &vpTex_);
    glBindTexture(GL_TEXTURE_2D, vpTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorEngine::DrawViewport()
{
    const int hintMode = renderMode_;
    ImGui::TextDisabled("[%s%s%s] %s   (RMB-drag + WASD/QE to fly, LMB to pick)",
                        kModes[hintMode],
                        hintMode == 0 ? " / " : "", hintMode == 0 ? kShading[ActiveWorld().GetScene().shadingModel] : "",
                        playing_ ? "Playing (PIE)" : "Editor World");
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = (int)avail.x, h = (int)avail.y;
    if (w < 16 || h < 16) return;

    // Apply the editor fly-camera to the active world's camera for this render.
    UWorld& world = ActiveWorld();
    ACamera& cam = world.GetCamera();
    cam.eye = camEye_;
    cam.SetOrientation(camYaw_, camPitch_);
    cam.SetFOV(60.0f, (float)w / (float)h);

    // Render per the toolbar Render Mode (Rasterizer / GPU RT / Hybrid) in both
    // editor and PIE. Rasterizer is the lit shaded preview (Flat/Gouraud/Phong).
    const int effMode = renderMode_;

    bool shown = false;
    if (effMode == 0)                                  // CPU lit rasterizer -> texture
    {
        UScene& scene = world.GetScene();
        scene.width = w; scene.height = h;
        FRenderShowFlag flag;
        flag.shading   = (EShadingModel)scene.shadingModel;   // Flat/Gouraud/Phong (HW6)
        flag.depthView = depthView_;
        renderer_.RasterShaded(world, flag);
        if (!scene.outputImage.empty())
        {
            EnsureViewportTex(w, h);
            glBindTexture(GL_TEXTURE_2D, vpTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, scene.outputImage.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            ImGui::Image((ImTextureID)(intptr_t)vpTex_, avail, ImVec2(0, 1), ImVec2(1, 0));
            shown = true;
        }
    }
    else                                               // GPU RT / Hybrid -> FBO
    {
        RenderWorldGPU(w, h, effMode);
        ImGui::Image((ImTextureID)(intptr_t)fboTex_, avail, ImVec2(0, 1), ImVec2(1, 0));
        shown = true;
    }

    if (shown)
    {
        // ---- transform gizmo (ImGuizmo) over the selected actor, editor only ----
        // ImGuizmo needs standard GL matrices, but the engine renders with the FCG
        // convention; we build a matching glm lookAt/perspective (same eye/fwd/up/
        // fov/aspect) purely to drive the gizmo overlay -- it lines up with the
        // rendered image in screen space. Edits decompose back into actor TRS.
        bool gizmoBusy = false;
        auto& actors = world.GetScene().Actors;
        if (!playing_ && selected_ >= 0 && selected_ < (int)actors.size())
        {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::AllowAxisFlip(false);   // keep axes fixed (no camera-facing flip)
            ImGuizmo::SetDrawlist();
            const ImVec2 rmin = ImGui::GetItemRectMin();
            ImGuizmo::SetRect(rmin.x, rmin.y, (float)w, (float)h);

            const glm::mat4 view = glm::lookAt(cam.eye, cam.eye - cam.w, cam.v);
            // Build the projection from the camera's actual frustum planes (l/r/b/t
            // at near distance d) -- matches the rendered image exactly and avoids
            // any radians/degrees ambiguity in glm::perspective.
            const glm::mat4 proj = glm::frustum(cam.l, cam.r, cam.b, cam.t, cam.d, 3000.0f);
            const ImGuizmo::OPERATION op = gizmoOp_ == 1 ? ImGuizmo::ROTATE
                                         : gizmoOp_ == 2 ? ImGuizmo::SCALE
                                                         : ImGuizmo::TRANSLATE;
            const ImGuizmo::MODE mode = gizmoLocal_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            AActor* a = actors[selected_];
            glm::mat4 model = a->rootComponent.GetWorldMatrix();
            const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                                      op, mode, glm::value_ptr(model));

            // Snapshot once at drag start: Manipulate only edits the local `model`
            // here -- the actor's TRS is still the pre-edit state until SetActor*
            // below -- so PushUndo() now captures the correct "before" world.
            const bool usingNow = ImGuizmo::IsUsing();
            if (usingNow && !gizmoWasUsing_) PushUndo();
            gizmoWasUsing_ = usingNow;

            if (changed)
            {
                glm::vec3 t, r, s;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), &t.x, &r.x, &s.x);
                a->SetActorLocation(t);
                a->SetActorRotation(r);
                a->SetActorScale(s);
                rtUploaded_ = false; hybridUploaded_ = false;   // geometry moved
            }
            gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        }

        // Green collision wireframes over the viewport (box edges + sphere rings).
        { const ImVec2 rm = ImGui::GetItemRectMin(); DrawColliders(cam, rm.x, rm.y, w, h); }

        // Latch fly-mode while RMB is held (so hover flicker during a drag does
        // not interrupt simultaneous rotate + WASD movement).
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            flying_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
            flying_ = false;

        if (flying_)
            UpdateEditorCamera(w, h);
        else if (!playing_ && !gizmoBusy && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            PickActor(w, h);

        // Mouse wheel over the viewport: dolly the camera along its forward axis
        // (zoom in/out). Shift = faster. -w is forward (ACamera convention).
        if (ImGui::IsItemHovered() && !flying_)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const float step = (ImGui::GetIO().KeyShift ? 4.0f : 1.2f) * wheel;
                camEye_ += (-cam.w) * step;
            }
        }
    }
}

void EditorEngine::UpdateEditorCamera(int /*w*/, int /*h*/)
{
    ImGuiIO& io = ImGui::GetIO();

    // RMB drag -> yaw/pitch (yaw matches cursor direction).
    camYaw_   += io.MouseDelta.x * 0.15f;
    camPitch_ -= io.MouseDelta.y * 0.15f;
    camPitch_  = (camPitch_ >  89.0f) ?  89.0f : (camPitch_ < -89.0f ? -89.0f : camPitch_);

    // WASD/QE fly along the current basis -- applied together with rotation.
    ACamera& cam = editorWorld_->GetCamera();
    cam.SetOrientation(camYaw_, camPitch_);
    const glm::vec3 fwd = -cam.w, right = cam.u, up = cam.v;
    const float speed = (io.KeyShift ? 14.0f : 5.0f) * io.DeltaTime;
    if (ImGui::IsKeyDown(ImGuiKey_W)) camEye_ += fwd   * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S)) camEye_ -= fwd   * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D)) camEye_ += right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A)) camEye_ -= right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_E)) camEye_ += up    * speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) camEye_ -= up    * speed;
}

void EditorEngine::PickActor(int w, int h)
{
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const float fx = (mp.x - mn.x) / (float)w;          // 0..1 left->right
    const float fy = (mp.y - mn.y) / (float)h;          // 0..1 top->bottom
    const int px = (int)(fx * w);
    const int py = (int)((1.0f - fy) * h);              // FBO/ray origin = bottom-left

    const ACamera& cam = ActiveWorld().GetCamera();
    const URay ray = cam.generateRay(px, py, w, h);

    auto& actors = ActiveWorld().GetScene().Actors;
    float best = 1e30f; int bestIdx = -1;
    for (int i = 0; i < (int)actors.size(); ++i)
    {
        UMeshComponent* mc = actors[i]->mesh;
        if (!mc || !mc->mesh) continue;
        float t, u, v; int tri;
        if (mc->intersect(ray, t, tri, u, v) && t < best) { best = t; bestIdx = i; }
    }
    if (bestIdx >= 0) selected_ = bestIdx;
}

void EditorEngine::DrawColliders(const ACamera& cam, float imgX, float imgY, int w, int h)
{
    // Only the SELECTED actor's collider, and only while its Collision component
    // is the one selected in Details (Unreal-style: collision shown on select).
    auto& actors = ActiveWorld().GetScene().Actors;
    if (detailComp_ != 2 || selected_ < 0 || selected_ >= (int)actors.size()) return;
    UPrimitiveComponent* sel = actors[selected_]->physics;
    if (!sel) return;

    const glm::mat4 view = glm::lookAt(cam.eye, cam.eye - cam.w, cam.v);
    const glm::mat4 proj = glm::frustum(cam.l, cam.r, cam.b, cam.t, cam.d, 3000.0f);
    const glm::mat4 vp   = proj * view;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = IM_COL32(60, 230, 90, 255);            // Unreal-ish collision green

    auto project = [&](const glm::vec3& p, ImVec2& out) -> bool
    {
        glm::vec4 c = vp * glm::vec4(p, 1.0f);
        if (c.w <= 1e-4f) return false;                      // behind camera
        glm::vec3 ndc = glm::vec3(c) / c.w;
        out = ImVec2(imgX + (ndc.x * 0.5f + 0.5f) * w,
                     imgY + (1.0f - (ndc.y * 0.5f + 0.5f)) * h);
        return true;
    };
    auto line = [&](const glm::vec3& a3, const glm::vec3& b3)
    {
        ImVec2 pa, pb;
        if (project(a3, pa) && project(b3, pb)) dl->AddLine(pa, pb, col, 1.5f);
    };

    {
        UPrimitiveComponent* p = sel;
        const glm::vec3 c = p->WorldCenter();                // collider center (actor loc + offset)

        if (p->GetShape() == EShape::Box)
        {
            const glm::vec3 he = static_cast<UBoxComponent*>(p)->halfExtents;
            glm::vec3 v[8];
            for (int i = 0; i < 8; ++i)
                v[i] = c + glm::vec3((i & 1) ? he.x : -he.x,
                                     (i & 2) ? he.y : -he.y,
                                     (i & 4) ? he.z : -he.z);
            const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4},
                                      {0,4},{1,5},{2,6},{3,7}};
            for (auto& e : edges) line(v[e[0]], v[e[1]]);
        }
        else if (p->GetShape() == EShape::Sphere)
        {
            const float r = static_cast<USphereComponent*>(p)->radius;
            const int N = 28;
            for (int ring = 0; ring < 3; ++ring)
                for (int i = 0; i < N; ++i)
                {
                    const float t0 = (float)i / N * 6.2831853f;
                    const float t1 = (float)(i + 1) / N * 6.2831853f;
                    glm::vec3 p0, p1;
                    if (ring == 0) { p0 = {std::cos(t0), std::sin(t0), 0}; p1 = {std::cos(t1), std::sin(t1), 0}; }
                    else if (ring == 1) { p0 = {std::cos(t0), 0, std::sin(t0)}; p1 = {std::cos(t1), 0, std::sin(t1)}; }
                    else { p0 = {0, std::cos(t0), std::sin(t0)}; p1 = {0, std::cos(t1), std::sin(t1)}; }
                    line(c + p0 * r, c + p1 * r);
                }
        }
    }
}

void EditorEngine::FocusActor(int idx)
{
    auto& actors = ActiveWorld().GetScene().Actors;
    if (idx < 0 || idx >= (int)actors.size()) return;
    AActor* a = actors[idx];
    selected_ = idx;

    glm::vec3 target = a->GetActorLocation();
    float dist = 6.0f;
    if (a->mesh && a->mesh->mesh && !a->mesh->mesh->vertices.empty())
    {
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (const Vertex& v : a->mesh->mesh->vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
        const glm::vec3 s = a->GetActorScale();
        target = a->GetActorLocation() + (mn + mx) * 0.5f * s;          // mesh centroid (no rotation)
        const glm::vec3 ext = (mx - mn) * s;
        dist = glm::max(4.0f, glm::max(ext.x, glm::max(ext.y, ext.z)) * 1.6f);
    }

    // Forward from the current yaw/pitch (matches ACamera::SetOrientation).
    const float cy = std::cos(glm::radians(camYaw_)),  sy = std::sin(glm::radians(camYaw_));
    const float cp = std::cos(glm::radians(camPitch_)), sp = std::sin(glm::radians(camPitch_));
    const glm::vec3 fwd(sy * cp, sp, -cy * cp);
    camEye_ = target - fwd * dist;                                       // look at the actor
}
