#pragma once
#include <string>
#include <vector>

class UMesh;

// ---------------------------------------------------------------------------
// UObjImporter — hand-written Wavefront .obj parser (NO external library).
// Produces a single UMesh: positions/uvs/normals are de-duplicated by the
// (v/vt/vn) index triple, faces are fan-triangulated, missing normals are
// computed from face normals. 1-based and negative OBJ indices are handled.
// mtllib/usemtl/o/g/s are ignored in this phase (default white material).
// ---------------------------------------------------------------------------
class UObjImporter
{
public:
    // Loads `path` into a new UMesh (caller owns it). Returns nullptr on a
    // missing/unreadable file; malformed lines are skipped (never crashes).
    // Ignores materials -- everything merges into one mesh with a default
    // material.
    static UMesh* Load(const char* path);

    // Material-aware load: parses mtllib/usemtl and SPLITS the file into one
    // UMesh per material used (1 mesh = 1 material), each carrying the Material
    // from the .mtl (Ka/Kd/Ks/Ns). A file with no usemtl yields a single mesh.
    // Caller owns every returned UMesh. Empty vector on missing/unreadable file.
    static std::vector<UMesh*> LoadMulti(const char* path);

    // One 'f' vertex token (e.g. "1/2/3", "1//3", "1/2", "1", "-3") parsed into
    // 0-based indices. Missing field -> -1. Negative OBJ indices resolve against
    // the current counts (np/nt/nn = vertices seen so far).
    struct IndexTriple { int p, t, n; };
    static IndexTriple ParseFaceToken(const std::string& tok, int np, int nt, int nn);
};
