#include "UTilemap.h"
#include "AActor.h"
#include "SphereSurface.h"
#include "CubeSurface.h"
#include "UPlayerCharacter.h"
#include "../Core/UScene.h"
#include "../Physics/PhysicalComponent.h"

#include <fstream>
#include <sstream>
#include <iostream>

// ---------------------------------------------------------------------------
// Utility: split a string on whitespace
// ---------------------------------------------------------------------------
static std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

// ---------------------------------------------------------------------------
// Utility: parse "key=value" — returns value string or ""
// ---------------------------------------------------------------------------
static std::string getValue(const std::string& token, const std::string& key)
{
    const std::string prefix = key + "=";
    if (token.rfind(prefix, 0) == 0)
        return token.substr(prefix.size());
    return "";
}

// ---------------------------------------------------------------------------
glm::vec3 UTilemap::parseVec3(const std::string& s)
{
    glm::vec3 v(0.0f);
    std::istringstream ss(s);
    char comma;
    ss >> v.x >> comma >> v.y >> comma >> v.z;
    return v;
}

// ---------------------------------------------------------------------------
bool UTilemap::parseDef(const std::string& line)
{
    auto tokens = split(line);
    // tokens[0] = symbol char (single character), tokens[1] = TYPE, rest = key=value
    if (tokens.size() < 2) return false;
    if (tokens[0].size() != 1) return false;

    char sym = tokens[0][0];
    TileDefinition def;

    const std::string& typeName = tokens[1];
    if      (typeName == "EMPTY")  def.type = ETileType::Empty;
    else if (typeName == "CUBE")   def.type = ETileType::Cube;
    else if (typeName == "SPHERE") def.type = ETileType::Sphere;
    else if (typeName == "PLAYER") def.type = ETileType::Player;
    else return false;

    for (size_t i = 2; i < tokens.size(); ++i)
    {
        const std::string& tok = tokens[i];

        std::string v;
        if      (!(v = getValue(tok, "kd"  )).empty()) def.kd        = parseVec3(v);
        else if (!(v = getValue(tok, "ka"  )).empty()) def.ka        = parseVec3(v);
        else if (!(v = getValue(tok, "ks"  )).empty()) def.ks        = parseVec3(v);
        else if (!(v = getValue(tok, "km"  )).empty()) def.km        = parseVec3(v);
        else if (!(v = getValue(tok, "half")).empty()) def.halfVec   = parseVec3(v);
        else if (!(v = getValue(tok, "shine")).empty()) def.shininess = std::stof(v);
        else if (!(v = getValue(tok, "r"   )).empty()) def.radius    = std::stof(v);
        else if (!(v = getValue(tok, "mass")).empty()) def.mass       = std::stof(v);
        else if (!(v = getValue(tok, "restitution")).empty()) def.restitution = std::stof(v);
        else if (!(v = getValue(tok, "friction"   )).empty()) def.friction    = std::stof(v);
        else if (!(v = getValue(tok, "tex" )).empty()) def.texPath   = v;
        else if (tok == "physics")  def.hasPhysics       = true;
        else if (tok == "nogravity") def.affectedByGravity = false;
    }

    defs[sym] = def;
    return true;
}

// ---------------------------------------------------------------------------
bool UTilemap::Load(const char* filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cout << "[UTilemap] Cannot open: " << filepath << "\n";
        return false;
    }

    defs.clear();
    layers.clear();

    int  currentLayer = -1;
    bool inLayer      = false;

    std::string line;
    while (std::getline(file, line))
    {
        // Strip carriage return (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip empty lines and comments
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

        auto tokens = split(line);
        if (tokens.empty()) continue;

        if (tokens[0] == "SCALE" && tokens.size() >= 2)
        {
            tileScale = std::stof(tokens[1]);
            inLayer = false;
        }
        else if (tokens[0] == "ORIGIN" && tokens.size() >= 4)
        {
            origin = glm::vec3(std::stof(tokens[1]),
                               std::stof(tokens[2]),
                               std::stof(tokens[3]));
            inLayer = false;
        }
        else if (tokens[0] == "DEFINE" && tokens.size() >= 3)
        {
            inLayer = false;
            // Pass everything after "DEFINE"
            parseDef(line.substr(line.find("DEFINE") + 7));
        }
        else if (tokens[0] == "LAYER" && tokens.size() >= 2)
        {
            currentLayer = std::stoi(tokens[1]);
            while ((int)layers.size() <= currentLayer)
                layers.emplace_back();
            inLayer = true;
        }
        else if (inLayer && currentLayer >= 0)
        {
            // This line is a row of tile symbols
            layers[currentLayer].push_back(tokens[0]);
        }
    }

    std::cout << "[UTilemap] Loaded " << filepath
              << " — " << layers.size() << " layer(s), "
              << defs.size() << " symbol(s)\n";
    return true;
}

// ---------------------------------------------------------------------------
// Spawn actors into scene
// ---------------------------------------------------------------------------
UPlayerCharacter* UTilemap::Spawn(UScene& scene) const
{
    UPlayerCharacter* player = nullptr;

    // Default half-extents based on scale (full tile = tileScale, half = tileScale/2)
    const float half = tileScale * 0.5f;

    for (int layerIdx = 0; layerIdx < (int)layers.size(); ++layerIdx)
    {
        const auto& rows = layers[layerIdx];

        for (int rowIdx = 0; rowIdx < (int)rows.size(); ++rowIdx)
        {
            const std::string& row = rows[rowIdx];

            for (int colIdx = 0; colIdx < (int)row.size(); ++colIdx)
            {
                char sym = row[colIdx];

                auto it = defs.find(sym);
                if (it == defs.end()) continue;

                const TileDefinition& def = it->second;
                if (def.type == ETileType::Empty) continue;

                // World position: bottom-left corner = origin, tile center offset by half
                glm::vec3 pos;
                pos.x = origin.x + colIdx  * tileScale + half;
                pos.y = origin.y + layerIdx * tileScale + half;
                pos.z = origin.z + rowIdx   * tileScale + half;

                if (def.type == ETileType::Player)
                {
                    if (!player)
                    {
                        player = new UPlayerCharacter();
                        player->position = pos;
                        scene.Actors.push_back(player);
                    }
                    continue;
                }

                AActor* actor = new AActor();
                actor->position = pos;

                if (def.type == ETileType::Cube)
                {
                    glm::vec3 h = def.halfVec;
                    // If user didn't override half, use default tile half
                    if (glm::length(h - glm::vec3(0.5f)) < 1e-4f) h = glm::vec3(half);

                    auto* surf = new CubeSurface(h);
                    surf->material.ka        = def.ka;
                    surf->material.kd        = def.kd;
                    surf->material.ks        = def.ks;
                    surf->material.km        = def.km;
                    surf->material.shininess = def.shininess;
                    if (!def.texPath.empty()) surf->SetTexture(def.texPath.c_str());
                    actor->SetSurface(surf);
                }
                else if (def.type == ETileType::Sphere)
                {
                    float r = (def.radius == 0.5f) ? half : def.radius;
                    auto* surf = new SphereSurface(r);
                    surf->material.ka        = def.ka;
                    surf->material.kd        = def.kd;
                    surf->material.ks        = def.ks;
                    surf->material.km        = def.km;
                    surf->material.shininess = def.shininess;
                    actor->SetSurface(surf);
                }

                if (def.hasPhysics)
                {
                    auto* phys = new PhysicalComponent(actor);
                    phys->mass              = def.mass;
                    phys->restitution       = def.restitution;
                    phys->friction          = def.friction;
                    phys->bAffectedByGravity = def.affectedByGravity;
                    actor->SetPhysics(phys);
                }

                scene.Actors.push_back(actor);
            }
        }
    }

    return player;
}
