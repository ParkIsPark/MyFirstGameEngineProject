// main.cpp -- entry point for a project built on MyFirstGameEngine.
//
// One executable, two roles (like Unreal's editor/standalone):
//   * EDITOR build  (no GAME_BUILD)  -> opens the ImGui editor on this project.
//   * GAME   build  (GAME_BUILD set) -> boots straight into the standalone game.
// Either build also honors `--game [world]` on the command line, so the editor's
// "Play (Window)" can spawn this same exe as a standalone game window.
//
// The boot reads <name>.proj -> Setting/DefaultEngine.ini -> Content/<StartupWorld>
// (Engine::Run wires this); GAME_BUILD just flips the no-argument default.

#include <string>
#include "EditorEngine.h"
#include "GameEngine.h"

static const char* kProj = "__PROJECT_NAME__.proj";

int main(int argc, char** argv)
{
    const std::string arg = (argc > 1) ? argv[1] : "";

    // Explicit standalone game request (editor Play-in-new-window spawns this).
    if (arg == "--game")
    {
        GameEngine game((argc > 2) ? argv[2] : "");   // ""=boot StartupWorld from .proj
        if (!game.Init(1280, 720, "__PROJECT_NAME__")) return -1;
        return game.Run(kProj);
    }

#ifdef GAME_BUILD
    // Packaged game: double-click -> play.
    GameEngine game("");
    if (!game.Init(1280, 720, "__PROJECT_NAME__")) return -1;
    return game.Run(kProj);
#else
    // Editor build: author the project.
    EditorEngine editor;
    if (!editor.Init(1280, 800, "__PROJECT_NAME__ Editor")) return -1;
    return editor.Run(kProj);
#endif
}
