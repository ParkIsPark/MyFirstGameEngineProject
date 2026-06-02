#include "EditorEngine.h"

#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>

// Placeholder scene for the Stage-1 shell (a real UWorld is bound in Stage 3).
namespace
{
    struct EdActor { const char* icon; const char* name; const char* tag; };
    const EdActor kActors[] = {
        { "[cam]", "Editor Camera",    "free" },
        { "[box]", "Cube_Floor",       "#1"   },
        { "[sph]", "Sphere_Ball",      "#2"   },
        { "[lit]", "DirectionalLight", "#3"   },
        { "[ps]",  "PlayerStart",      "#4"   },
    };
    const int kActorCount = (int)(sizeof(kActors) / sizeof(kActors[0]));
    const char* kModes[] = { "Rasterizer", "GPU RT", "Hybrid" };
}

EditorEngine::~EditorEngine()
{
    if (imguiReady_)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void EditorEngine::OnStartup()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                 // don't write imgui.ini next to the exe
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    imguiReady_ = true;
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

void EditorEngine::DrawUI()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->WorkPos;
    const ImVec2 sz  = vp->WorkSize;
    const float toolH = 40.0f;
    const float leftW = 230.0f, rightW = 290.0f;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    // ---- Toolbar (top strip) ----
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sz.x, toolH), ImGuiCond_Always);
    if (ImGui::Begin("##Toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar))
    {
        if (!playing_) { if (ImGui::Button("|>  Play"))  playing_ = true; }
        else           { if (ImGui::Button("[]  Stop"))  playing_ = false; }
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("Render Mode"); ImGui::SameLine();
        for (int i = 0; i < 3; ++i)
        {
            if (i) ImGui::SameLine();
            if (ImGui::RadioButton(kModes[i], renderMode_ == i)) renderMode_ = i;
        }
        ImGui::SameLine(sz.x - 180);
        ImGui::Text("FPS: %.0f  (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::SameLine(); ImGui::Checkbox("Demo", &showDemo_);
    }
    ImGui::End();

    const float bodyY = pos.y + toolH;
    const float bodyH = sz.y - toolH;

    // ---- World Outliner (left) ----
    ImGui::SetNextWindowPos(ImVec2(pos.x, bodyY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftW, bodyH), ImGuiCond_Always);
    if (ImGui::Begin("World Outliner", nullptr, fixed))
    {
        for (int i = 0; i < kActorCount; ++i)
        {
            char label[96];
            std::snprintf(label, sizeof(label), "%s %s", kActors[i].icon, kActors[i].name);
            if (ImGui::Selectable(label, selected_ == i)) selected_ = i;
            ImGui::SameLine(leftW - 56); ImGui::TextDisabled("%s", kActors[i].tag);
        }
    }
    ImGui::End();

    // ---- Viewport (center) ----
    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, bodyY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sz.x - leftW - rightW, bodyH), ImGuiCond_Always);
    if (ImGui::Begin("Viewport", nullptr, fixed))
    {
        ImGui::TextDisabled("[%s] %s", kModes[renderMode_], playing_ ? "Playing (PIE)" : "Editor World");
        ImGui::Separator();
        ImGui::TextWrapped("Scene render-to-FBO is wired in Stage 2 (this panel will show "
                           "the URenderer output via ImGui::Image).");
    }
    ImGui::End();

    // ---- Details (right) ----
    ImGui::SetNextWindowPos(ImVec2(pos.x + sz.x - rightW, bodyY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightW, bodyH), ImGuiCond_Always);
    if (ImGui::Begin("Details", nullptr, fixed))
    {
        if (selected_ < 0) { ImGui::TextDisabled("Select an actor in the World Outliner."); }
        else
        {
            ImGui::Text("%s", kActors[selected_].name);
            ImGui::Separator();
            if (playing_) ImGui::TextColored(ImVec4(0.88f, 0.66f, 0.35f, 1.0f),
                                             "PIE mode -- properties read-only");
            static float p[3] = { 0, -1, 0 }, r[3] = { 0, 0, 0 }, s[3] = { 4, 0.25f, 4 };
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", p, 0.1f);
            ImGui::DragFloat3("Rotation", r, 1.0f);
            ImGui::DragFloat3("Scale",    s, 0.05f);
        }
    }
    ImGui::End();

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}
