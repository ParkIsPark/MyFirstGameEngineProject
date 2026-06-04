#pragma once
#include "Engine.h"
#include "UWorldRenderer.h"
#include <string>

// ---------------------------------------------------------------------------
// GameEngine (P9) — the standalone GAME runtime: an Engine with NO editor/ImGui.
// It loads a .world, runs the world lifecycle (BeginPlay -> physics + actor tick),
// and renders it with the world's stored render mode (Rasterizer / GPU RT /
// Hybrid) via UWorldRenderer.
//
// This is what `Test.exe --game <world>` launches, what the editor's
// "Play (Window)" spawns as a separate process, and what a packaged game
// double-clicks into. Keep it dependency-light (no Editor/ImGui linkage).
// ---------------------------------------------------------------------------
class GameEngine : public Engine
{
public:
    explicit GameEngine(std::string worldPath) : worldPath_(std::move(worldPath)) {}

protected:
    void    OnStartup() override;     // init the world renderer (GPU passes)
    UWorld* WorldSetting() override;  // load worldPath_ (.world) -> UWorld
    void    Render() override;        // render per the world's render mode

private:
    std::string    worldPath_;
    UWorldRenderer worldRenderer_;
};
