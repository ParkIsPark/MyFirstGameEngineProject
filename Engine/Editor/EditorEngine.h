#pragma once
#include "Engine.h"

// ---------------------------------------------------------------------------
// EditorEngine (page 7) — an Engine variant that overlays a Dear ImGui editor
// (Unreal-style: Toolbar / World Outliner / Details / Viewport). It reuses the
// base Engine's window + GL context + main loop; OnStartup() boots ImGui and
// Render() draws the panels each frame.
//
// MVP is incremental: Stage 1 = ImGui shell + panel layout. Later stages add
// the FBO viewport, outliner/details bound to a UWorld, picking, and the
// Editor<->PIE (Play-in-Editor) world switch.
// ---------------------------------------------------------------------------
class EditorEngine : public Engine
{
public:
    ~EditorEngine() override;

protected:
    void OnStartup() override;
    void Render()    override;

private:
    void DrawUI();

    bool imguiReady_  = false;
    bool showDemo_    = false;
    int  renderMode_  = 0;       // 0=Rasterizer 1=GPU RT 2=Hybrid
    bool playing_     = false;   // Editor vs PIE (Stage 5)
    int  selected_    = -1;      // selected outliner row (Stage 3)
};
