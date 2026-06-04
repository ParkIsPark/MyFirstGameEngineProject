#pragma once
#include "Engine.h"
#include "URenderer.h"
#include <string>

// ---------------------------------------------------------------------------
// GameEngine (P9) — the standalone GAME runtime: an Engine with NO editor/ImGui.
// It loads a .world, runs the world lifecycle (BeginPlay -> physics + actor tick),
// and renders the world's chosen shading model to the window (CPU raster, GL 3.3).
//
// This is what `Test.exe --game <world>` launches, and what the editor's
// "Play (New Window)" spawns as a separate process. A packaged game double-clicks
// straight into this path. Keep it dependency-light (no Editor/ImGui linkage).
// ---------------------------------------------------------------------------
class GameEngine : public Engine
{
public:
    explicit GameEngine(std::string worldPath) : worldPath_(std::move(worldPath)) {}

protected:
    UWorld* WorldSetting() override;   // load worldPath_ (.world) -> UWorld
    void    Render() override;         // shade the world -> glDrawPixels

private:
    std::string worldPath_;
    URenderer   renderer_;
};
