#pragma once
#include <string>
struct Material;

// ---------------------------------------------------------------------------
// UMaterial — shared material-asset resolver (the Material analogue of
// UMesh::Resolve). A `.material` is the engine-native, editable material asset
// (FArchive-serialized Material); `.mtl` (Wavefront) is also read.
//
// Resolve() caches by path and returns ONE shared Material* per path, so every
// UMeshComponent that references the same path points at the same instance —
// editing it (material editor / Details) updates every object that uses it. The
// cache owns the pointers (freed never; lifetime = process, like UMesh::Resolve).
// ---------------------------------------------------------------------------
namespace UMaterial
{
    // Shared material for a Content path (.material native / .mtl read). The
    // diffuse texture pixels are loaded from diffuseTexPath so the result is
    // render-ready (CPU raster + GPU upload). nullptr only on empty path.
    Material* Resolve(const std::string& path);

    // Write a Material to a native .material file (numeric + diffuseTexPath; the
    // texture pixels are not embedded). Refreshes the cache entry so the shared
    // instance and the file agree. Returns false on I/O failure.
    bool Save(const std::string& path, const Material& m);

    // (Re)load the diffuse texture pixels into m from m.diffuseTexPath (sRGB kept
    // raw; SampleDiffuse converts). No-op when the path is empty.
    void LoadTexture(Material& m);
}
