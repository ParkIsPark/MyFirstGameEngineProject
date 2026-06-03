#pragma once
#include "Engine.h"
#include "UWorld.h"
#include "URenderer.h"
#include "UMeshRayTracer.h"
#include "UHybridPass.h"
#include "UGBuffer.h"
#include "URasterizer.h"
#include "ThreadPool.h"

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
    void SaveWorld();                    // editor world -> Content/<name>.world
    void OnPlay();                       // Editor -> PIE: deep-copy + BeginPlay
    void OnStop();                       // PIE -> Editor
    UWorld* CopyWorld(UWorld& src);      // deep copy (shares UMesh assets)
    UWorld& ActiveWorld() { return (playing_ && pieWorld_) ? *pieWorld_ : editorWorld_; }
    void DrawUI();
    void DrawMenuBar();
    void DrawToolbar();
    void DrawOutliner();
    void DrawDetails();
    void DrawViewport();
    void DrawContentBrowser();
    void DrawStatusBar(float x, float y, float w, float h);
    void ScanContent();
    void EnsureViewportTex(int w, int h);
    void EnsureFBO(int w, int h);                 // FBO for the GPU render modes
    void RenderWorldGPU(int w, int h, int mode);  // mode 1=GPU RT, 2=Hybrid -> fbo_
    void UpdateEditorCamera(int w, int h);   // RMB-fly + WASD (when viewport active)
    void PickActor(int w, int h);            // left-click ray pick

    bool imguiReady_ = false;
    bool showDemo_   = false;
    int  renderMode_ = 0;        // 0=Rasterizer 1=GPU RT 2=Hybrid
    bool depthView_  = false;    // CPU raster preview: grayscale depth instead of shade
    bool playing_    = false;    // Editor vs PIE (Stage 5)
    int  selected_   = -1;       // index into editorWorld_ actors
    std::string worldName_ = "EditorWorld";   // -> Content/<worldName_>.world

    UWorld    editorWorld_;
    UWorld*   pieWorld_ = nullptr;      // spawned on Play (deep copy of editorWorld_)
    URenderer renderer_;

    std::vector<UMesh*>      meshAssets_;   // owned shared mesh assets
    std::vector<std::string> actorNames_;   // parallel to scene.Actors

    struct ContentEntry { std::string name; const char* cat; const char* icon; };
    std::vector<ContentEntry> content_;     // scanned Content/ assets
    int  cbFilter_ = 0;                     // 0=All 1=World 2=Mesh 3=Texture

    unsigned int vpTex_  = 0;    // viewport texture (CPU outputImage upload, raster)
    int          vpTexW_ = 0;
    int          vpTexH_ = 0;

    // GPU render modes (PIE): render the world into this FBO, then ImGui::Image it
    UMeshRayTracer worldRT_;     // GPU RT play mode
    UHybridPass    hybrid_;      // Hybrid play mode (G-buffer + shadow)
    URasterizer    rast_;        // builds the hybrid G-buffer
    UGBuffer       gbuf_;
    ThreadPool     pool_;        // tile-parallel G-buffer fill (hybrid mode)

    // Geometry-upload cache. The BVH + triangle TBOs depend only on the scene
    // geometry (mesh identity + world transform + albedo), NOT the camera, so we
    // rebuild/upload them only when that signature changes -- not every frame.
    // (The hybrid G-buffer raster + upload is camera-dependent and still runs
    // each frame; only its shadow-ray BVH is cached here.)
    size_t rtUploadSig_     = 0;  bool rtUploaded_     = false;  // GPU RT mode
    size_t hybridUploadSig_ = 0;  bool hybridUploaded_ = false;  // Hybrid mode
    unsigned int   fbo_ = 0, fboTex_ = 0, fboDepth_ = 0;
    int            fboW_ = 0, fboH_ = 0;
    bool           gpuReady_ = false;

    // editor fly-camera state (applied to editorWorld_'s camera each frame)
    glm::vec3 camEye_   = glm::vec3(0.0f, 0.0f, 0.0f);
    float     camYaw_   = 0.0f;   // degrees; 0 looks into -Z
    float     camPitch_ = 0.0f;
    bool      flying_   = false;  // RMB held since pressed over the viewport
};
