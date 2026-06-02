#pragma once
#include <vector>

class UMesh;

// ---------------------------------------------------------------------------
// UFbxImporter — loads Autodesk FBX (and any other Assimp-supported format)
// via Assimp, converting each scene mesh into a UMesh. ONLY the binary parsing
// uses a library; the transform pipeline / rasterizer / shading stay 100%
// in-engine (state this in the presentation).
//
// Output is the same UMesh used everywhere else, so imported FBX meshes flow
// through the identical UMeshComponent + rasterizer + GPU-RT pipeline as the
// generators and the OBJ importer.
// ---------------------------------------------------------------------------
class UFbxImporter
{
public:
    struct LoadOptions
    {
        float globalScale = 1.0f;   // multiply positions (FBX cm -> engine m: 0.01)
        bool  flipUV      = true;   // FBX usually has a flipped V axis
        bool  swapYZ      = false;  // Z-up source -> Y-up engine: (x,y,z)->(x,z,-y)
    };

    // Returns one UMesh per scene mesh (caller owns each). Empty vector on a
    // missing/unreadable/corrupt file (error is logged, never crashes).
    static std::vector<UMesh*> Load(const char* path, const LoadOptions& opt = {});
};
