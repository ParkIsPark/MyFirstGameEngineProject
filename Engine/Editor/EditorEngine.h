#pragma once
#include "Engine.h"
#include "UWorld.h"
#include "URenderer.h"

#include <vector>
#include <string>

class UMesh;
class AActor;

// ---------------------------------------------------------------------------
// EditorEngine (page 7) — an Engine variant that overlays a Dear ImGui editor
// (Unreal-style: Toolbar / World Outliner / Details / Viewport) over a live
// UWorld. Reuses the base Engine's window + GL context + main loop.
//
//   Stage 1  ImGui shell + panel layout
//   Stage 2  Viewport renders the scene into an FBO -> ImGui::Image
//   Stage 3  Outliner/Details bound to a real UWorld; editing reflects live
// ---------------------------------------------------------------------------
class EditorEngine : public Engine
{
public:
    ~EditorEngine() override;

protected:
    void OnStartup() override;
    void Render()    override;

private:
    void BuildEditorWorld();
    void DrawUI();
    void DrawToolbar();
    void DrawOutliner();
    void DrawDetails();
    void DrawViewport();
    void EnsureViewportTex(int w, int h);

    bool imguiReady_ = false;
    bool showDemo_   = false;
    int  renderMode_ = 0;        // 0=Rasterizer 1=GPU RT 2=Hybrid
    bool playing_    = false;    // Editor vs PIE (Stage 5)
    int  selected_   = -1;       // index into editorWorld_ actors

    UWorld    editorWorld_;
    URenderer renderer_;

    std::vector<UMesh*>      meshAssets_;   // owned shared mesh assets
    std::vector<std::string> actorNames_;   // parallel to scene.Actors

    unsigned int vpTex_  = 0;    // viewport texture (outputImage upload)
    int          vpTexW_ = 0;
    int          vpTexH_ = 0;
};
