#pragma once
#include "Engine.h"
#include "UWorld.h"
#include "FRenderQuality.h"
#include "UWorldRenderer.h"
#include "BuildManager.h"
#include "FEditorAssetWorkflow.h"
#include "../Developer/FDeveloperWorldRenderRoute.h"

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
    EditorEngine() : Engine(Role::Editor) {}
    ~EditorEngine() override;
    const FWorldRendererStats& RendererStats() const { return worldRenderer_.Stats(); }

protected:
    void OnStartup() override;
    void OnShutdown() override;
    void Render()    override;

private:
    void BuildEditorWorld();
    void SaveWorld();                    // editor world -> Content/<name>.world
    void NewWorld();                     // replace editor world with an empty one
    void NewMaterial();                  // write a fresh Content/Material_N.material
    void ApplyMaterialToSelected(const std::string& path);  // assign a material asset to the selection
    // Save a material to a native .material (a .mtl source is written as <stem>.material,
    // never overwritten). Refreshes the browser; returns the path actually written.
    std::string SaveMaterialAsset(const std::string& path, const Material& m);
    void LoadWorld(const std::string& path);   // replace editor world from a .world
    void SetEditorWorld(UWorld* w, const std::string& name);  // swap + rebind UI state

    // Undo/redo via whole-world snapshots (FWorldSerializer text). PushUndo()
    // captures the CURRENT state before a mutation; Undo/Redo swap states.
    void PushUndo();
    void Undo();
    void Redo();
    void ClearHistory();
    AActor* AddActor(const char* type, const std::string& name);  // factory spawn
    void CopySelected();                  // Ctrl+C -> clone selected into clipboard
    void PasteClipboard();                // Ctrl+V -> spawn a clone of the clipboard
    void DeleteSelected();                // Delete -> remove the selected actor
    void ImportAsset(const std::string& path);  // .obj/.fbx -> mesh actor; .world -> load
    UMesh* LoadMeshFile(const std::string& path);  // .obj/.fbx/.mesh/descriptor -> UMesh*
    void RebuildActorNames();            // resync actorNames_ from scene actors
    void OnPlay();                       // Editor -> PIE: deep-copy + BeginPlay (in-window)
    void OnStop();                       // PIE -> Editor
    void LaunchGameProcess();            // save world -> spawn argv0 --game (new window)
    // Lossless deep copy (name + transform + mesh[shared] + material + physics +
    // lights + camera + shadingModel). resetPhysics zeroes velocity (for PIE).
protected:
    UWorld* CopyWorld(UWorld& src, bool resetPhysics = false);
    AActor* CloneActor(AActor* src, bool resetPhysics = false);  // deep copy one actor
private:
    UWorld& ActiveWorld() { return (playing_ && pieWorld_) ? *pieWorld_ : *editorWorld_; }
    void DrawUI();
    void DrawMenuBar();
    void DrawToolbar();
    void DrawOutliner();
    void DrawDetails();
    void DrawViewport();
    void DrawContentBrowser();
    void DrawBuildLog();                  // background-build output panel
    void DrawProjectSettings();           // set the DefaultWorld (Setting/DefaultEngine.ini)
    void DrawMaterialEditor();            // double-click a .material -> edit the shared asset
    bool DrawMaterialFields(Material& m); // shared kd/ks/shininess/mirror/texture widgets (returns changed)
    void DrawRenderSettings();            // AA / GI quality popup (persisted to ini)
    void LoadRenderSettings();            // Config/EditorSettings.ini -> members
    void SaveRenderSettings();            // members -> Config/EditorSettings.ini
    void DrawStatusBar(float x, float y, float w, float h);
    void ScanContent();
    // Copy an imported asset (mesh/texture and its sidecars) into Content/ so the
    // project still finds it on the next launch. Returns the in-Content path (or
    // the original path if it is already inside Content/ or cannot be copied).
    std::string CopyToContent(const std::string& src);
    // Re-parent an actor in the scene graph (outliner drag-drop). Keeps the child's
    // world position; parent==nullptr detaches to the world root. Rejects cycles.
    void SetActorParent(AActor* child, AActor* parent);
    void UpdateEditorCamera(int w, int h);   // RMB-fly + WASD (when viewport active)
    void PickActor(int w, int h);            // left-click ray pick
    void FocusActor(int idx);                // frame the editor camera on an actor
    // Draw green collision-shape wireframes over the viewport (Unreal-style).
    void DrawColliders(const ACamera& cam, float imgX, float imgY, int w, int h);

    // OS file drag-drop -> ImportAsset (routes through the window user-pointer).
    static void dropTrampoline(struct GLFWwindow* win, int count, const char** paths);

    bool imguiReady_   = false;
    bool showDemo_     = false;
    bool showBuildLog_ = false;
    bool showRenderSettings_ = false;
    bool showDeveloperSettings_ = false;
    FDeveloperSettings developerSettings_;
    FDeveloperOverrideController developerOverride_{CreateDeveloperWorldRenderRoute};
    bool showMatEditor_ = false;          // material editor window open
    std::string matEditPath_;             // Content path of the material being edited
    bool showProjectSettings_ = false;    // Project Settings window open
    // Render settings (ini-persisted): independent Editor + Game profiles. The
    // editor viewport uses editorRS_, the played game (PIE + standalone) uses
    // gameRS_, so the editing view and the game can render differently.
    FRenderQuality editorRS_;
    FRenderQuality gameRS_;
    int            rsTab_ = 0;    // Render Settings window: 0 = Editor, 1 = Game
    FRenderQuality& activeRS() { return playing_ ? gameRS_ : editorRS_; }
    BuildManager buildMgr_;
    bool depthView_  = false;    // CPU raster preview: grayscale depth instead of shade
    int  gizmoOp_    = 0;        // ImGuizmo op: 0=Translate 1=Rotate 2=Scale
    bool gizmoLocal_ = false;    // gizmo space: false=World, true=Local
    bool playing_    = false;    // Editor vs PIE (Stage 5)
    int  selected_   = -1;       // index into editorWorld_ actors
    int  detailComp_ = 0;        // selected component in Details: 0 Actor 1 Mesh 2 Collision 3 Light
    std::string worldName_ = "EditorWorld";   // Content-relative stem -> Content/<worldName_>.world

    std::vector<UWorld*> undoStack_, redoStack_;   // in-memory world snapshots (clones)
    bool gizmoWasUsing_ = false;              // latch: snapshot once per gizmo drag

    UWorld*   editorWorld_ = nullptr;   // the live edited world (replaceable: New/Load)
    UWorld*   pieWorld_    = nullptr;    // spawned on Play (deep copy of editorWorld_)
    AActor*   clipboard_   = nullptr;    // Ctrl+C/Ctrl+V actor clipboard (a clone)
    UWorldRenderer worldRenderer_;
    FRenderTarget viewportTarget_;

    std::vector<UMesh*>      meshAssets_;   // owned shared mesh assets
    std::vector<std::string> actorNames_;   // parallel to scene.Actors

    std::vector<FEditorContentAsset> content_; // recursive scan, paths relative to Content/
    std::string contentDir_ = "Content";    // dir ScanContent actually read from
    int  cbFilter_ = 0;                     // 0=All, then category tabs in DrawContentBrowser
    int  cbRename_ = -1;                     // content entry index being renamed (-1 none)
    char cbBuf_[128] = {};                   // rename / import-path text buffer
    std::string scriptValidation_;           // most recent Script Details validation

    // editor fly-camera state (applied to editorWorld_'s camera each frame)
    glm::vec3 camEye_   = glm::vec3(0.0f, 0.0f, 0.0f);
    float     camYaw_   = 0.0f;   // degrees; 0 looks into -Z
    float     camPitch_ = 0.0f;
    bool      flying_   = false;  // RMB held since pressed over the viewport
};
