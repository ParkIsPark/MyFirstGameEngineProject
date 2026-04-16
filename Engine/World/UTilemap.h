#pragma once
#include <string>
#include <map>
#include <vector>
#include <glm/glm.hpp>

class UScene;
class UPlayerCharacter;

// ---------------------------------------------------------------------------
// Tile type
// ---------------------------------------------------------------------------
enum class ETileType { Empty, Cube, Sphere, Player };

// ---------------------------------------------------------------------------
// Sub-structs for tile definition
// ---------------------------------------------------------------------------
struct FTileGeometry
{
    glm::vec3 halfVec      = glm::vec3(0.5f);  // Cube half-extents
    float     radius       = 0.5f;             // Sphere radius
    bool      halfExplicit = false;            // true when set via geometry block
    bool      radExplicit  = false;
};

struct FTileMaterial
{
    glm::vec3   ka        = glm::vec3(0.1f);
    glm::vec3   kd        = glm::vec3(0.8f);
    glm::vec3   ks        = glm::vec3(0.1f);
    glm::vec3   km        = glm::vec3(0.0f);
    float       shininess = 16.0f;
    std::string texPath;
};

struct FTilePhysics
{
    bool  hasPhysics        = false;
    bool  bStatic           = false;
    float mass              = 1.0f;
    float restitution       = 0.5f;
    float friction          = 0.3f;
    bool  affectedByGravity = true;
};

// ---------------------------------------------------------------------------
// Combined tile definition
// ---------------------------------------------------------------------------
struct TileDefinition
{
    ETileType     type     = ETileType::Empty;
    FTileGeometry geometry;
    FTileMaterial material;
    FTilePhysics  physics;
};

// ---------------------------------------------------------------------------
// One placed instance (world-space)
// ---------------------------------------------------------------------------
struct FTileInstance
{
    std::string   symbolName;
    glm::vec3     worldPos    = glm::vec3(0.f);
    bool          hasOverride = false;
    TileDefinition override_def;   // merged (base + overrides); valid only when hasOverride
};

// ---------------------------------------------------------------------------
// Parsed document — pure data container
// ---------------------------------------------------------------------------
struct FTilemapDocument
{
    float     tileScale = 2.0f;
    glm::vec3 origin    = glm::vec3(0.f);

    std::map<std::string, TileDefinition> symbolTable;
    std::vector<FTileInstance>            instances;
};

// ---------------------------------------------------------------------------
// Public façade — unchanged Load/Spawn API
// ---------------------------------------------------------------------------
class UTilemap
{
public:
    // Parse a .tilemap file. Detects old vs. new format automatically.
    bool Load(const char* filepath);

    // Instantiate actors into scene.
    // Returns the spawned UPlayerCharacter, or nullptr if none defined.
    UPlayerCharacter* Spawn(UScene& scene) const;

private:
    FTilemapDocument doc;
};
