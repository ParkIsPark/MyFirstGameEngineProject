#include "EditorEngine.h"

#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glm/glm.hpp>
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
#include "PointLight.h"
#include "LightComponent.h"

#include <cstdio>
#include <cctype>
#include <filesystem>
#include <algorithm>

namespace { const char* kModes[] = { "Rasterizer", "GPU RT", "Hybrid" }; }

EditorEngine::~EditorEngine()
{
    if (imguiReady_)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    for (UMesh* m : meshAssets_) delete m;   // UScene dtor deletes the actors
}

// ---------------------------------------------------------------------------
void EditorEngine::BuildEditorWorld()
{
    UMesh* sphere = UMesh::GenerateSphere(1.6f, 28, 14);
    UMesh* cube   = UMesh::GenerateCube(glm::vec3(1.4f));
    meshAssets_.push_back(sphere);
    meshAssets_.push_back(cube);

    auto spawnMesh = [&](const char* name, UMesh* mesh, glm::vec3 pos, glm::vec3 kd)
    {
        AActor* a = new AActor();
        a->position = pos;
        UMeshComponent* mc = new UMeshComponent();
        mc->mesh = mesh;
        mc->hasMaterialOverride = true;
        mc->materialOverride.kd        = kd;
        mc->materialOverride.ks        = glm::vec3(0.4f);
        mc->materialOverride.shininess = 32.0f;
        a->mesh = mc;
        editorWorld_.Spawn(a);
        actorNames_.push_back(name);
    };
    auto spawnEmpty = [&](const char* name, glm::vec3 pos)
    {
        AActor* a = new AActor();
        a->position = pos;
        editorWorld_.Spawn(a);
        actorNames_.push_back(name);
    };

    spawnMesh("Sphere_Ball", sphere, glm::vec3(-2.6f, 2.5f, -9.0f), glm::vec3(0.45f, 0.55f, 0.90f));
    spawnMesh("Cube_Box",    cube,   glm::vec3( 2.6f, 0.0f, -9.0f), glm::vec3(0.90f, 0.55f, 0.28f));

    // A real light actor (ALight owns a LightComponent) -- inspectable in Details.
    {
        ALight* light = new ALight(new PointLight(glm::vec3(5, 5, -3), glm::vec3(1.0f), glm::vec3(1.0f)));
        light->position = glm::vec3(5.0f, 5.0f, -3.0f);
        editorWorld_.Spawn(light);
        actorNames_.push_back("PointLight");
    }
    spawnEmpty("PlayerStart", glm::vec3(0.0f, -1.0f, -6.0f));

    // Ball: dynamic sphere collider (falls under gravity in PIE).
    {
        AActor* ball = editorWorld_.GetScene().Actors[0];
        USphereComponent* sc = new USphereComponent(ball);
        sc->radius = 1.6f; sc->mass = 1.0f; sc->restitution = 0.4f;
        ball->SetPhysics(sc);
    }
    // Cube: static box collider (mass 0) -- inspectable, doesn't move.
    {
        AActor* box = editorWorld_.GetScene().Actors[1];
        UBoxComponent* bc = new UBoxComponent(box);
        bc->halfExtents = glm::vec3(1.4f); bc->mass = 0.0f;
        box->SetPhysics(bc);
    }

    selected_ = 0;
}

UWorld* EditorEngine::CopyWorld(UWorld& src)
{
    UWorld* dst = new UWorld();
    for (AActor* sa : src.GetScene().Actors)
    {
        AActor* da = new AActor();
        da->position = sa->position;
        da->rotation = sa->rotation;
        da->scale    = sa->scale;

        if (sa->mesh)
        {
            UMeshComponent* mc = new UMeshComponent();
            *mc = *sa->mesh;                 // shares the UMesh asset; copies override + rel xform
            da->mesh = mc;
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
                p->velocity = glm::vec3(0.0f);          // PIE starts at rest
                da->SetPhysics(p);
            }
        }
        dst->Spawn(da);
    }
    return dst;
}

void EditorEngine::OnPlay()
{
    pieWorld_ = CopyWorld(editorWorld_);               // deep copy (UMesh shared)
    pieWorld_->GetPhysics().enableFloor = true;
    pieWorld_->GetPhysics().floorY      = -2.5f;       // ball lands here
    pieWorld_->BeginPlay();
    playing_ = true;
}

void EditorEngine::OnStop()
{
    if (pieWorld_) { pieWorld_->EndPlay(); delete pieWorld_; pieWorld_ = nullptr; }
    playing_ = false;
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

    BuildEditorWorld();
    ScanContent();
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

    // Gather the world's mesh instances (+ per-instance albedo) and the light.
    std::vector<const UMesh*> meshes;
    std::vector<glm::mat4>    models;
    std::vector<glm::vec3>    albedos;
    glm::vec3 lightPos(6, 8, 2), lightColor(1.0f);
    for (AActor* a : scene.Actors)
    {
        if (UMeshComponent* mc = a->mesh)
            if (mc->mesh)
            {
                meshes.push_back(mc->mesh);
                models.push_back(mc->GetWorldMatrix(*a));
                albedos.push_back(mc->hasMaterialOverride ? mc->materialOverride.kd : mc->mesh->material.kd);
            }
        if (ALight* L = dynamic_cast<ALight*>(a))
            if (PointLight* pl = dynamic_cast<PointLight*>(L->lightComp))
            { lightPos = pl->LightPos; lightColor = pl->LightColor * pl->LightIntensity; }
    }

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
        { mix(&meshes[i], sizeof(meshes[i])); mix(&models[i], sizeof(glm::mat4)); mix(&albedos[i], sizeof(glm::vec3)); }
    }

    EnsureFBO(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (mode == 1)                                   // GPU RT
    {
        if (!rtUploaded_ || geomSig != rtUploadSig_)   // skip when geometry static
        {
            worldRT_.UploadWorld(meshes, models, albedos, lightPos, lightColor);
            rtUploadSig_ = geomSig; rtUploaded_ = true;
        }
        worldRT_.SetLight(lightPos, lightColor);     // light may move without geometry
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
                                          cx0, cy0, cx1, cy1, /*countStats=*/false);
            }
        });

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

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

void EditorEngine::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File"))
    {
        ImGui::MenuItem("New World");
        ImGui::MenuItem("Save World");
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(window_, GL_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))   { ImGui::MenuItem("Undo"); ImGui::MenuItem("Redo"); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("World"))  { ImGui::MenuItem("World Settings"); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Build"))  { ImGui::MenuItem("Build Lighting"); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Window")) { ImGui::MenuItem("Reset Layout"); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Help"))   { ImGui::MenuItem("About"); ImGui::EndMenu(); }

    const char* title = "MyEngine Editor -- DefaultWorld.world";
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x - 16);
    ImGui::TextDisabled("%s", title);
    ImGui::EndMainMenuBar();
}

void EditorEngine::DrawContentBrowser()
{
    const char* tabs[] = { "All", "World", "Mesh", "Texture" };
    for (int i = 0; i < 4; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(tabs[i], cbFilter_ == i)) cbFilter_ = i; }
    ImGui::SameLine(); ImGui::TextDisabled("   Content/");
    ImGui::Separator();

    const float cell = 96.0f;
    const int cols = (int)(ImGui::GetContentRegionAvail().x / cell);
    int shown = 0;
    for (const ContentEntry& e : content_)
    {
        if (cbFilter_ != 0 && std::string(e.cat) != tabs[cbFilter_]) continue;
        if (shown % (cols < 1 ? 1 : cols) != 0) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Button((std::string(e.icon) + "##" + e.name).c_str(), ImVec2(74, 52));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 80);
        ImGui::TextWrapped("%s", e.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ++shown;
    }
    if (shown == 0) ImGui::TextDisabled("(no assets in this filter)");
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
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Render Mode"); ImGui::SameLine();
    for (int i = 0; i < 3; ++i) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(kModes[i], renderMode_ == i)) renderMode_ = i; }
    const float fps = ImGui::GetIO().Framerate;
    ImGui::SameLine(ImGui::GetWindowWidth() - 210);
    ImGui::Text("FPS %.0f  (%.2f ms)", fps, 1000.0f / fps);
    ImGui::SameLine(); ImGui::Checkbox("Demo", &showDemo_);
}

void EditorEngine::DrawOutliner()
{
    auto& actors = ActiveWorld().GetScene().Actors;
    ImGui::TextDisabled("%d actors", (int)actors.size());
    ImGui::Separator();
    for (int i = 0; i < (int)actors.size(); ++i)
    {
        const bool isMesh = actors[i]->mesh != nullptr;
        char label[128];
        std::snprintf(label, sizeof(label), "%s %s", isMesh ? "[M]" : "[*]", actorNames_[i].c_str());
        if (ImGui::Selectable(label, selected_ == i)) selected_ = i;
    }
}

void EditorEngine::DrawDetails()
{
    auto& actors = ActiveWorld().GetScene().Actors;
    if (selected_ < 0 || selected_ >= (int)actors.size())
    { ImGui::TextDisabled("Select an actor in the World Outliner."); return; }

    AActor* a = actors[selected_];
    ImGui::Text("%s", actorNames_[selected_].c_str());
    ImGui::Separator();
    if (playing_)
        ImGui::TextColored(ImVec4(0.88f, 0.66f, 0.35f, 1), "PIE mode -- read-only");

    ImGui::BeginDisabled(playing_);          // properties are read-only during PIE
    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &a->position.x, 0.05f);
    ImGui::DragFloat3("Rotation", &a->rotation.x, 1.0f);
    ImGui::DragFloat3("Scale",    &a->scale.x,    0.05f, 0.01f, 100.0f);

    // ---- UMeshComponent ----
    if (a->mesh && a->mesh->mesh)
    {
        Material& m = a->mesh->hasMaterialOverride ? a->mesh->materialOverride
                                                   : a->mesh->mesh->material;
        ImGui::SeparatorText("Mesh Component");
        ImGui::Text("Triangles: %d", a->mesh->mesh->triangleCount());
        ImGui::ColorEdit3("Diffuse",   &m.kd.x);
        ImGui::ColorEdit3("Specular",  &m.ks.x);
        ImGui::DragFloat ("Shininess", &m.shininess, 1.0f, 0.0f, 256.0f);
    }

    // ---- UPrimitiveComponent (collision shape + rigid body) ----
    if (UPrimitiveComponent* p = a->physics)
    {
        const char* shape = p->GetShape() == EShape::Sphere ? "Sphere Collision"
                          : p->GetShape() == EShape::Box    ? "Box Collision"
                                                            : "Capsule Collision";
        ImGui::SeparatorText(shape);
        ImGui::TextDisabled("UPrimitiveComponent");
        ImGui::DragFloat("Mass",        &p->mass,        0.1f,  0.0f, 100.0f);
        ImGui::DragFloat("Restitution", &p->restitution, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Friction",    &p->friction,    0.01f, 0.0f, 1.0f);
        ImGui::Checkbox ("Affected by Gravity", &p->bAffectedByGravity);
        if (p->GetShape() == EShape::Sphere)
            ImGui::DragFloat ("Radius",       &static_cast<USphereComponent*>(p)->radius, 0.05f, 0.01f, 100.0f);
        else if (p->GetShape() == EShape::Box)
            ImGui::DragFloat3("Half Extents", &static_cast<UBoxComponent*>(p)->halfExtents.x, 0.05f, 0.01f, 100.0f);
    }

    // ---- LightComponent (ALight) ----
    if (ALight* light = dynamic_cast<ALight*>(a))
    {
        if (LightComponent* lc = light->lightComp)
        {
            ImGui::SeparatorText("Light Component");
            ImGui::ColorEdit3("Light Color",  &lc->LightColor.x);
            ImGui::DragFloat3("Intensity",    &lc->LightIntensity.x, 0.05f, 0.0f, 50.0f);
            if (PointLight* pl = dynamic_cast<PointLight*>(lc))
                ImGui::DragFloat3("Light Pos", &pl->LightPos.x, 0.1f);
        }
    }

    // ---- Add Component ----
    ImGui::Spacing();
    if (ImGui::Button("+ Add Component"))
        ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent"))
    {
        if (!a->physics && ImGui::MenuItem("Sphere Collision"))
        { auto* s = new USphereComponent(a); s->radius = 1.0f; a->SetPhysics(s); }
        if (!a->physics && ImGui::MenuItem("Box Collision"))
        { auto* b = new UBoxComponent(a); b->halfExtents = glm::vec3(1.0f); a->SetPhysics(b); }
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
    ImGui::TextDisabled("[%s] %s   (RMB-drag + WASD/QE to fly, LMB to pick)",
                        kModes[renderMode_], playing_ ? "Playing (PIE)" : "Editor World");
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

    // Editor world: always lit rasterizer (fast editing preview).
    // PIE (playing): render per the toolbar Render Mode (Rasterizer/GPU RT/Hybrid).
    const int effMode = playing_ ? renderMode_ : 0;

    bool shown = false;
    if (effMode == 0)                                  // CPU lit rasterizer -> texture
    {
        UScene& scene = world.GetScene();
        scene.width = w; scene.height = h;
        renderer_.Render(world, ERenderMode::RasterOnly);
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
        // Latch fly-mode while RMB is held (so hover flicker during a drag does
        // not interrupt simultaneous rotate + WASD movement).
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            flying_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
            flying_ = false;

        if (flying_)
            UpdateEditorCamera(w, h);
        else if (!playing_ && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            PickActor(w, h);
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
    ACamera& cam = editorWorld_.GetCamera();
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
        if (mc->intersect(ray, *actors[i], t, tri, u, v) && t < best) { best = t; bestIdx = i; }
    }
    if (bestIdx >= 0) selected_ = bestIdx;
}
