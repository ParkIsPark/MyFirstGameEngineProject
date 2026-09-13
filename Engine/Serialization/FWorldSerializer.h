#pragma once
#include <functional>
#include <string>
#include <string_view>

class UWorld;

// ---------------------------------------------------------------------------
// FWorldSerializer (P6) — .world save / load (text, section blocks).
//
// Format: WorldFormat / [World] (ShadingModel) / [Camera] (Position/Yaw/Pitch/
// Fov) / [Actor] (Type/Name/transform) followed by its [Component] children
// (each Type + its Serialize fields). Save iterates the scene; Load creates
// actors/components via the factories and Serialize(read), attaching each
// component under its actor's root, then wires typed pointers (mesh / light).
// Robust: malformed input never throws (FArchive contract).
// ---------------------------------------------------------------------------
namespace FWorldSerializer
{
    using FWarningSink = std::function<void(std::string_view)>;

    std::string Save(UWorld& world);
    bool        SaveToFile(UWorld& world, const char* path);
    UWorld*     Load(const std::string& text);     // caller owns the world
    UWorld*     LoadFromFile(const char* path);
    // Empty restores the default stderr diagnostic behavior.
    void        SetWarningSink(FWarningSink sink);
}
