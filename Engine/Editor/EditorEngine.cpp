#include "EditorEngine.h"

#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glm/glm.hpp>
#include "UMesh.h"
#include "UMeshComponent.h"
#include "AActor.h"
#include "ACamera.h"
#include "UScene.h"
#include "URay.h"

#include <cstdio>

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

    spawnMesh("Sphere_Ball", sphere, glm::vec3(-2.6f, 0.0f, -9.0f), glm::vec3(0.45f, 0.55f, 0.90f));
    spawnMesh("Cube_Box",    cube,   glm::vec3( 2.6f, 0.0f, -9.0f), glm::vec3(0.90f, 0.55f, 0.28f));
    spawnEmpty("DirectionalLight", glm::vec3(5.0f, 5.0f, -3.0f));
    spawnEmpty("PlayerStart",      glm::vec3(0.0f, -1.0f, -6.0f));

    selected_ = 0;
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

    BuildEditorWorld();
}

void EditorEngine::Render()
{
    if (!imguiReady_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    DrawUI();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------------------
void EditorEngine::DrawUI()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->WorkPos, sz = vp->WorkSize;
    const float toolH = 40.0f, leftW = 230.0f, rightW = 300.0f;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(ImVec2(sz.x, toolH));
    if (ImGui::Begin("##Toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar)) DrawToolbar();
    ImGui::End();

    const float by = pos.y + toolH, bh = sz.y - toolH;
    ImGui::SetNextWindowPos(ImVec2(pos.x, by)); ImGui::SetNextWindowSize(ImVec2(leftW, bh));
    if (ImGui::Begin("World Outliner", nullptr, fixed)) DrawOutliner();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, by));
    ImGui::SetNextWindowSize(ImVec2(sz.x - leftW - rightW, bh));
    if (ImGui::Begin("Viewport", nullptr, fixed)) DrawViewport();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + sz.x - rightW, by));
    ImGui::SetNextWindowSize(ImVec2(rightW, bh));
    if (ImGui::Begin("Details", nullptr, fixed)) DrawDetails();
    ImGui::End();

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

void EditorEngine::DrawToolbar()
{
    if (!playing_) { if (ImGui::Button("|>  Play")) playing_ = true; }
    else           { if (ImGui::Button("[]  Stop")) playing_ = false; }
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
    auto& actors = editorWorld_.GetScene().Actors;
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
    auto& actors = editorWorld_.GetScene().Actors;
    if (selected_ < 0 || selected_ >= (int)actors.size())
    { ImGui::TextDisabled("Select an actor in the World Outliner."); return; }

    AActor* a = actors[selected_];
    ImGui::Text("%s", actorNames_[selected_].c_str());
    ImGui::Separator();
    if (playing_)
        ImGui::TextColored(ImVec4(0.88f, 0.66f, 0.35f, 1), "PIE mode -- read-only");

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &a->position.x, 0.05f);
    ImGui::DragFloat3("Rotation", &a->rotation.x, 1.0f);
    ImGui::DragFloat3("Scale",    &a->scale.x,    0.05f, 0.01f, 100.0f);

    if (a->mesh && a->mesh->mesh)
    {
        Material& m = a->mesh->hasMaterialOverride ? a->mesh->materialOverride
                                                   : a->mesh->mesh->material;
        ImGui::SeparatorText("Material");
        ImGui::ColorEdit3("Diffuse",   &m.kd.x);
        ImGui::ColorEdit3("Specular",  &m.ks.x);
        ImGui::DragFloat ("Shininess", &m.shininess, 1.0f, 0.0f, 256.0f);
    }
    else
    {
        ImGui::SeparatorText("Component");
        ImGui::TextDisabled("(no mesh component)");
    }
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

    // Apply the editor fly-camera to the world camera for this frame's render.
    ACamera& cam = editorWorld_.GetCamera();
    cam.eye = camEye_;
    cam.SetOrientation(camYaw_, camPitch_);
    cam.SetFOV(60.0f, (float)w / (float)h);

    // Render the world (CPU rasterizer for now; GPU RT / Hybrid in a later stage).
    UScene& scene = editorWorld_.GetScene();
    scene.width = w; scene.height = h;
    renderer_.Render(editorWorld_, ERenderMode::RasterOnly);   // -> scene.outputImage

    if (!scene.outputImage.empty())
    {
        EnsureViewportTex(w, h);
        glBindTexture(GL_TEXTURE_2D, vpTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, scene.outputImage.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        ImGui::Image((ImTextureID)(intptr_t)vpTex_, avail, ImVec2(0, 1), ImVec2(1, 0));

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

    const ACamera& cam = editorWorld_.GetCamera();
    const URay ray = cam.generateRay(px, py, w, h);

    auto& actors = editorWorld_.GetScene().Actors;
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
