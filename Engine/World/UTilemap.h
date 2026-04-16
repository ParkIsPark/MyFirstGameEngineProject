#pragma once
#include <string>
#include <map>
#include <vector>
#include <glm/glm.hpp>

class UScene;
class UPlayerCharacter;

// ---------------------------------------------------------------------------
// Data for one symbol in the DEFINE section
// ---------------------------------------------------------------------------
enum class ETileType { Empty, Cube, Sphere, Player };

struct TileDefinition
{
    ETileType type      = ETileType::Empty;

    // Geometry
    glm::vec3 halfVec   = glm::vec3(0.5f);   // Cube: per-axis half-extents
    float     radius    = 0.5f;              // Sphere: radius

    // Phong material
    glm::vec3 ka        = glm::vec3(0.1f);
    glm::vec3 kd        = glm::vec3(0.8f);
    glm::vec3 ks        = glm::vec3(0.1f);
    glm::vec3 km        = glm::vec3(0.0f);
    float     shininess = 16.0f;
    std::string texPath;

    // Physics
    bool  hasPhysics       = false;
    float mass             = 1.0f;
    float restitution      = 0.5f;
    float friction         = 0.3f;
    bool  affectedByGravity = true;
};

// ---------------------------------------------------------------------------
// Tilemap loader + spawner
// ---------------------------------------------------------------------------
class UTilemap
{
public:
    // Parse a .tilemap text file.  Returns false on IO error.
    bool Load(const char* filepath);

    // Instantiate actors into scene.
    // Returns the spawned UPlayerCharacter, or nullptr if none defined.
    UPlayerCharacter* Spawn(UScene& scene) const;

private:
    float     tileScale = 2.0f;
    glm::vec3 origin    = glm::vec3(0.0f);

    std::map<char, TileDefinition>       defs;
    std::vector<std::vector<std::string>> layers; // layers[y][row] = "###..."

    // Parse a single DEFINE line (everything after the keyword)
    bool parseDef(const std::string& line);

    // Parse a vec3 from "r,g,b"
    static glm::vec3 parseVec3(const std::string& s);
};
