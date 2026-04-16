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

// ============================================================
//  String / token utilities
// ============================================================

static std::vector<std::string> splitWS(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// Strip inline comments (// or #)
static std::string stripComment(const std::string& line)
{
    auto pos = line.find("//");
    std::string s = (pos != std::string::npos) ? line.substr(0, pos) : line;
    pos = s.find('#');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// Tokenize: split on whitespace, treat { } ; as individual tokens
static std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (c == '{' || c == '}' || c == ';')
        {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            out.push_back(std::string(1, c));
        }
        else if (std::isspace((unsigned char)c))
        {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
        else
        {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static glm::vec3 parseVec3Tokens(const std::vector<std::string>& tok, size_t& i)
{
    glm::vec3 v(0.f);
    if (i < tok.size()) v.x = std::stof(tok[i++]);
    if (i < tok.size()) v.y = std::stof(tok[i++]);
    if (i < tok.size()) v.z = std::stof(tok[i++]);
    return v;
}

// ============================================================
//  Block parsers shared by new-format parser and inline-override
// ============================================================

static void parseGeometryBlock(const std::vector<std::string>& tok, size_t& i, FTileGeometry& g)
{
    // Called with i pointing past the opening "{"
    while (i < tok.size() && tok[i] != "}")
    {
        if (tok[i] == ";") { ++i; continue; }
        std::string key = tok[i++];
        if (key == "half" && i + 2 < tok.size())
        {
            g.halfVec      = parseVec3Tokens(tok, i);
            g.halfExplicit = true;
        }
        else if (key == "radius" && i < tok.size())
        {
            g.radius      = std::stof(tok[i++]);
            g.radExplicit = true;
        }
        // unknown keys skipped
    }
    if (i < tok.size()) ++i; // consume "}"
}

static void parseMaterialBlock(const std::vector<std::string>& tok, size_t& i, FTileMaterial& m)
{
    while (i < tok.size() && tok[i] != "}")
    {
        if (tok[i] == ";") { ++i; continue; }
        std::string key = tok[i++];
        if      (key == "kd"    && i + 2 < tok.size()) m.kd        = parseVec3Tokens(tok, i);
        else if (key == "ka"    && i + 2 < tok.size()) m.ka        = parseVec3Tokens(tok, i);
        else if (key == "ks"    && i + 2 < tok.size()) m.ks        = parseVec3Tokens(tok, i);
        else if (key == "km"    && i + 2 < tok.size()) m.km        = parseVec3Tokens(tok, i);
        else if (key == "shine" && i < tok.size())     m.shininess = std::stof(tok[i++]);
        else if (key == "tex"   && i < tok.size())     m.texPath   = tok[i++];
        // unknown keys skipped
    }
    if (i < tok.size()) ++i;
}

static void parsePhysicsBlock(const std::vector<std::string>& tok, size_t& i, FTilePhysics& p)
{
    p.hasPhysics = true;
    while (i < tok.size() && tok[i] != "}")
    {
        if (tok[i] == ";") { ++i; continue; }
        std::string key = tok[i++];
        if      (key == "mass"        && i < tok.size()) p.mass              = std::stof(tok[i++]);
        else if (key == "restitution" && i < tok.size()) p.restitution       = std::stof(tok[i++]);
        else if (key == "friction"    && i < tok.size()) p.friction          = std::stof(tok[i++]);
        else if (key == "static"      && i < tok.size()) p.bStatic           = (tok[i++] == "true");
        else if (key == "nogravity")                      p.affectedByGravity = false;
        // unknown keys skipped
    }
    if (i < tok.size()) ++i;
}

// Apply flat key-value overrides (for inline "place ... at ... { ... }")
static void applyFlatOverride(const std::vector<std::string>& tok, size_t& i, TileDefinition& def)
{
    // i should point past the opening "{"
    while (i < tok.size() && tok[i] != "}")
    {
        if (tok[i] == ";") { ++i; continue; }
        std::string key = tok[i++];
        if      (key == "mass"        && i < tok.size()) def.physics.mass        = std::stof(tok[i++]);
        else if (key == "restitution" && i < tok.size()) def.physics.restitution = std::stof(tok[i++]);
        else if (key == "friction"    && i < tok.size()) def.physics.friction    = std::stof(tok[i++]);
        else if (key == "kd"          && i + 2 < tok.size()) def.material.kd    = parseVec3Tokens(tok, i);
        else if (key == "ka"          && i + 2 < tok.size()) def.material.ka    = parseVec3Tokens(tok, i);
        else if (key == "ks"          && i + 2 < tok.size()) def.material.ks    = parseVec3Tokens(tok, i);
        else ++i; // skip unknown value
    }
    if (i < tok.size()) ++i; // consume "}"
}

// ============================================================
//  UTilemapSpawner
// ============================================================

class UTilemapSpawner
{
public:
    static UPlayerCharacter* Spawn(const FTilemapDocument& doc, UScene& scene)
    {
        UPlayerCharacter* player  = nullptr;
        const float       tilehalf = doc.tileScale * 0.5f;

        for (const FTileInstance& inst : doc.instances)
        {
            auto it = doc.symbolTable.find(inst.symbolName);
            if (it == doc.symbolTable.end()) continue;

            const TileDefinition& def = inst.hasOverride ? inst.override_def : it->second;

            if (def.type == ETileType::Empty) continue;

            if (def.type == ETileType::Player)
            {
                if (!player)
                {
                    player           = new UPlayerCharacter();
                    player->position = inst.worldPos;
                    scene.Actors.push_back(player);
                }
                else
                {
                    std::cout << "[UTilemapSpawner] Warning: duplicate PLAYER at ("
                              << inst.worldPos.x << ","
                              << inst.worldPos.y << ","
                              << inst.worldPos.z << ") — ignored\n";
                }
                continue;
            }

            AActor* actor    = new AActor();
            actor->position  = inst.worldPos;

            if (def.type == ETileType::Cube)
            {
                glm::vec3 h = def.geometry.halfExplicit
                            ? def.geometry.halfVec
                            : glm::vec3(tilehalf);

                auto* surf             = new CubeSurface(h);
                surf->material.ka        = def.material.ka;
                surf->material.kd        = def.material.kd;
                surf->material.ks        = def.material.ks;
                surf->material.km        = def.material.km;
                surf->material.shininess = def.material.shininess;
                if (!def.material.texPath.empty())
                    surf->SetTexture(def.material.texPath.c_str());
                actor->SetSurface(surf);
            }
            else if (def.type == ETileType::Sphere)
            {
                float r = def.geometry.radExplicit
                        ? def.geometry.radius
                        : tilehalf;

                auto* surf             = new SphereSurface(r);
                surf->material.ka        = def.material.ka;
                surf->material.kd        = def.material.kd;
                surf->material.ks        = def.material.ks;
                surf->material.km        = def.material.km;
                surf->material.shininess = def.material.shininess;
                actor->SetSurface(surf);
            }

            if (def.physics.hasPhysics)
            {
                auto* phys               = new PhysicalComponent(actor);
                phys->mass               = def.physics.mass;
                phys->restitution        = def.physics.restitution;
                phys->friction           = def.physics.friction;
                phys->bAffectedByGravity = def.physics.affectedByGravity;
                actor->SetPhysics(phys);
            }

            scene.Actors.push_back(actor);
        }

        return player;
    }
};

// ============================================================
//  FTilemapParser_Legacy  (old single-char symbol format)
// ============================================================

class FTilemapParser_Legacy
{
public:
    static bool Parse(const std::string& filepath, FTilemapDocument& out)
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        out = FTilemapDocument{};

        std::map<char, TileDefinition>           charDefs;
        std::vector<std::pair<int, std::string>> layerRows; // (layerIdx, rowStr)
        int  currentLayer = -1;
        bool inLayer      = false;

        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

            auto tokens = splitWS(line);
            if (tokens.empty()) continue;

            if (tokens[0] == "SCALE" && tokens.size() >= 2)
            {
                out.tileScale = std::stof(tokens[1]);
                inLayer = false;
            }
            else if (tokens[0] == "ORIGIN" && tokens.size() >= 4)
            {
                out.origin = glm::vec3(std::stof(tokens[1]),
                                       std::stof(tokens[2]),
                                       std::stof(tokens[3]));
                inLayer = false;
            }
            else if (tokens[0] == "DEFINE" && tokens.size() >= 3)
            {
                inLayer = false;
                auto pos = line.find("DEFINE");
                if (pos != std::string::npos)
                    parseLegacyDef(line.substr(pos + 7), charDefs);
            }
            else if (tokens[0] == "LAYER" && tokens.size() >= 2)
            {
                currentLayer = std::stoi(tokens[1]);
                inLayer      = true;
            }
            else if (inLayer && currentLayer >= 0)
            {
                layerRows.emplace_back(currentLayer, tokens[0]);
            }
        }

        // Promote char-keyed defs to string-keyed symbol table
        for (auto& [c, def] : charDefs)
            out.symbolTable[std::string(1, c)] = def;

        // Convert grid rows to world-space instances
        std::map<int, std::vector<std::string>> layerMap;
        for (auto& [layerIdx, row] : layerRows)
            layerMap[layerIdx].push_back(row);

        const float tilehalf = out.tileScale * 0.5f;

        for (auto& [layerIdx, rows] : layerMap)
        {
            for (int rowIdx = 0; rowIdx < (int)rows.size(); ++rowIdx)
            {
                const std::string& row = rows[rowIdx];
                for (int colIdx = 0; colIdx < (int)row.size(); ++colIdx)
                {
                    std::string symStr(1, row[colIdx]);
                    if (out.symbolTable.find(symStr) == out.symbolTable.end()) continue;

                    FTileInstance inst;
                    inst.symbolName  = symStr;
                    inst.worldPos.x  = out.origin.x + colIdx   * out.tileScale + tilehalf;
                    inst.worldPos.y  = out.origin.y + layerIdx  * out.tileScale + tilehalf;
                    inst.worldPos.z  = out.origin.z + rowIdx    * out.tileScale + tilehalf;
                    out.instances.push_back(inst);
                }
            }
        }

        std::cout << "[UTilemap] Legacy format — "
                  << out.symbolTable.size() << " symbol(s), "
                  << out.instances.size()   << " instance(s)\n";
        return true;
    }

private:
    static glm::vec3 parseCommaVec3(const std::string& s)
    {
        glm::vec3 v(0.f);
        std::istringstream ss(s);
        char comma;
        ss >> v.x >> comma >> v.y >> comma >> v.z;
        return v;
    }

    static std::string getLegacyValue(const std::string& token, const std::string& key)
    {
        const std::string prefix = key + "=";
        if (token.rfind(prefix, 0) == 0) return token.substr(prefix.size());
        return "";
    }

    static void parseLegacyDef(const std::string& line,
                                std::map<char, TileDefinition>& charDefs)
    {
        auto tokens = splitWS(line);
        if (tokens.size() < 2 || tokens[0].size() != 1) return;

        char sym = tokens[0][0];
        TileDefinition def;

        if      (tokens[1] == "EMPTY")  def.type = ETileType::Empty;
        else if (tokens[1] == "CUBE")   def.type = ETileType::Cube;
        else if (tokens[1] == "SPHERE") def.type = ETileType::Sphere;
        else if (tokens[1] == "PLAYER") def.type = ETileType::Player;
        else return;

        for (size_t i = 2; i < tokens.size(); ++i)
        {
            const std::string& tok = tokens[i];
            std::string v;

            if      (!(v = getLegacyValue(tok, "kd"         )).empty())
            {
                def.material.kd = parseCommaVec3(v);
            }
            else if (!(v = getLegacyValue(tok, "ka"         )).empty())
            {
                def.material.ka = parseCommaVec3(v);
            }
            else if (!(v = getLegacyValue(tok, "ks"         )).empty())
            {
                def.material.ks = parseCommaVec3(v);
            }
            else if (!(v = getLegacyValue(tok, "km"         )).empty())
            {
                def.material.km = parseCommaVec3(v);
            }
            else if (!(v = getLegacyValue(tok, "half"       )).empty())
            {
                def.geometry.halfVec      = parseCommaVec3(v);
                def.geometry.halfExplicit = true;
            }
            else if (!(v = getLegacyValue(tok, "shine"      )).empty())
            {
                def.material.shininess = std::stof(v);
            }
            else if (!(v = getLegacyValue(tok, "r"          )).empty())
            {
                def.geometry.radius    = std::stof(v);
                def.geometry.radExplicit = true;
            }
            else if (!(v = getLegacyValue(tok, "mass"       )).empty())
            {
                def.physics.mass = std::stof(v);
            }
            else if (!(v = getLegacyValue(tok, "restitution")).empty())
            {
                def.physics.restitution = std::stof(v);
            }
            else if (!(v = getLegacyValue(tok, "friction"   )).empty())
            {
                def.physics.friction = std::stof(v);
            }
            else if (!(v = getLegacyValue(tok, "tex"        )).empty())
            {
                def.material.texPath = v;
            }
            else if (tok == "physics" )   def.physics.hasPhysics       = true;
            else if (tok == "nogravity")  def.physics.affectedByGravity = false;
        }

        charDefs[sym] = def;
    }
};

// ============================================================
//  FTilemapParser  (new @HEADER / @SYMBOLS / @DEFS / @INSTANCES format)
// ============================================================

class FTilemapParser
{
public:
    static bool Parse(const std::string& filepath, FTilemapDocument& out)
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        out = FTilemapDocument{};

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(stripComment(line));
        }

        parseHeader   (lines, out);
        parseSymbols  (lines, out);
        parseDefs     (lines, out);
        parseInstances(lines, out);

        std::cout << "[UTilemap] New format — "
                  << out.symbolTable.size() << " symbol(s), "
                  << out.instances.size()   << " instance(s)\n";
        return true;
    }

private:
    // Extract lines between a section tag and the next @END
    static std::vector<std::string> extractSection(const std::vector<std::string>& lines,
                                                    const std::string& tag)
    {
        std::vector<std::string> out;
        bool inside = false;
        for (const std::string& l : lines)
        {
            auto t = splitWS(l);
            if (!t.empty() && t[0] == tag  ) { inside = true;  continue; }
            if (!t.empty() && t[0] == "@END") { inside = false; continue; }
            if (inside) out.push_back(l);
        }
        return out;
    }

    // ── @HEADER ─────────────────────────────────────────────────────────────
    static void parseHeader(const std::vector<std::string>& lines, FTilemapDocument& doc)
    {
        for (const std::string& l : extractSection(lines, "@HEADER"))
        {
            auto t = splitWS(l);
            if (t.empty()) continue;
            if (t[0] == "scale"  && t.size() >= 2)
                doc.tileScale = std::stof(t[1]);
            if (t[0] == "origin" && t.size() >= 4)
                doc.origin = glm::vec3(std::stof(t[1]), std::stof(t[2]), std::stof(t[3]));
        }
    }

    // ── @SYMBOLS ─────────────────────────────────────────────────────────────
    static void parseSymbols(const std::vector<std::string>& lines, FTilemapDocument& doc)
    {
        for (const std::string& l : extractSection(lines, "@SYMBOLS"))
        {
            auto t = splitWS(l);
            if (t.size() < 2) continue;

            TileDefinition def;
            if      (t[1] == "CUBE")   def.type = ETileType::Cube;
            else if (t[1] == "SPHERE") def.type = ETileType::Sphere;
            else if (t[1] == "PLAYER") def.type = ETileType::Player;
            else if (t[1] == "EMPTY")  def.type = ETileType::Empty;
            else continue;

            doc.symbolTable[t[0]] = def;
        }
    }

    // ── @DEFS ────────────────────────────────────────────────────────────────
    static void parseDefs(const std::vector<std::string>& rawLines, FTilemapDocument& doc)
    {
        // Concatenate section text and lex into a token stream
        std::string combined;
        for (const std::string& l : extractSection(rawLines, "@DEFS"))
            combined += l + " ";

        auto tok = tokenize(combined);
        size_t i = 0;

        while (i < tok.size())
        {
            if (tok[i] != "define") { ++i; continue; }
            ++i; // consume "define"
            if (i >= tok.size()) break;

            std::string name = tok[i++];

            // Expect opening "{"
            if (i >= tok.size() || tok[i] != "{") continue;
            ++i;

            // Inherit type already declared in @SYMBOLS (if any)
            TileDefinition def;
            auto it = doc.symbolTable.find(name);
            if (it != doc.symbolTable.end()) def = it->second;

            // Parse sub-blocks until closing "}"
            while (i < tok.size() && tok[i] != "}")
            {
                if (tok[i] == ";" || tok[i] == "{") { ++i; continue; }
                std::string block = tok[i++];

                // Sub-block must be followed by "{"
                if (i >= tok.size() || tok[i] != "{") continue;
                ++i; // consume sub-block "{"

                if      (block == "geometry") parseGeometryBlock(tok, i, def.geometry);
                else if (block == "material") parseMaterialBlock(tok, i, def.material);
                else if (block == "physics" ) parsePhysicsBlock (tok, i, def.physics);
                else
                {
                    // Skip unknown sub-block (balance braces)
                    int depth = 1;
                    while (i < tok.size() && depth > 0)
                    {
                        if      (tok[i] == "{") ++depth;
                        else if (tok[i] == "}") --depth;
                        ++i;
                    }
                }
            }
            if (i < tok.size()) ++i; // consume closing "}" of define

            doc.symbolTable[name] = def;
        }
    }

    // ── @INSTANCES ───────────────────────────────────────────────────────────
    static void parseInstances(const std::vector<std::string>& rawLines, FTilemapDocument& doc)
    {
        auto section = extractSection(rawLines, "@INSTANCES");

        const float tilehalf  = doc.tileScale * 0.5f;
        bool        inGrid    = false;
        int         gridLayer = 0;
        glm::vec3   gridOrigin = doc.origin;
        int         gridRow   = 0;

        for (const std::string& l : section)
        {
            auto t = splitWS(l);
            if (t.empty()) continue;

            // Standalone opening brace (grid block opens on next line)
            if (t[0] == "{") continue;

            // Closing brace — end of current grid block
            if (t[0] == "}")
            {
                inGrid = false;
                continue;
            }

            // Grid block header: grid [layer=N] [origin=X Y Z] [{]
            if (t[0] == "grid")
            {
                gridLayer  = 0;
                gridOrigin = doc.origin;
                gridRow    = 0;

                for (size_t k = 1; k < t.size(); ++k)
                {
                    if (t[k].rfind("layer=", 0) == 0)
                    {
                        gridLayer = std::stoi(t[k].substr(6));
                    }
                    else if (t[k] == "origin" && k + 3 < t.size())
                    {
                        gridOrigin.x = std::stof(t[k + 1]);
                        gridOrigin.y = std::stof(t[k + 2]);
                        gridOrigin.z = std::stof(t[k + 3]);
                        k += 3;
                    }
                }

                inGrid = true;
                continue;
            }

            // Explicit placement: place <sym> at X Y Z [{ overrides }]
            // Requires at least 6 tokens: place sym at x y z
            if (t[0] == "place" && t.size() >= 6 && t[2] == "at")
            {
                FTileInstance inst;
                inst.symbolName = t[1];
                inst.worldPos   = glm::vec3(std::stof(t[3]),
                                             std::stof(t[4]),
                                             std::stof(t[5]));

                // Look for optional inline override block starting with "{"
                for (size_t k = 6; k < t.size(); ++k)
                {
                    if (t[k] == "{")
                    {
                        auto symIt = doc.symbolTable.find(inst.symbolName);
                        if (symIt != doc.symbolTable.end())
                        {
                            inst.override_def = symIt->second;
                            inst.hasOverride  = true;

                            // Build token list from "{" onward and apply overrides
                            std::vector<std::string> ovTok(t.begin() + k, t.end());
                            size_t oi = 1; // skip the leading "{"
                            applyFlatOverride(ovTok, oi, inst.override_def);
                        }
                        break;
                    }
                }

                doc.instances.push_back(inst);
                continue;
            }

            // Grid row (space-separated symbol names, "." = empty)
            if (inGrid)
            {
                for (int colIdx = 0; colIdx < (int)t.size(); ++colIdx)
                {
                    if (t[colIdx] == ".") continue;

                    FTileInstance inst;
                    inst.symbolName = t[colIdx];
                    inst.worldPos.x = gridOrigin.x + colIdx    * doc.tileScale + tilehalf;
                    inst.worldPos.y = gridOrigin.y + gridLayer  * doc.tileScale + tilehalf;
                    inst.worldPos.z = gridOrigin.z + gridRow    * doc.tileScale + tilehalf;
                    doc.instances.push_back(inst);
                }
                ++gridRow;
            }
        }
    }
};

// ============================================================
//  UTilemap  (public façade)
// ============================================================

bool UTilemap::Load(const char* filepath)
{
    doc = FTilemapDocument{};

    // Peek at the first meaningful token to detect format version
    std::ifstream probe(filepath);
    if (!probe.is_open())
    {
        std::cout << "[UTilemap] Cannot open: " << filepath << "\n";
        return false;
    }

    std::string firstToken;
    std::string line;
    while (std::getline(probe, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto t = splitWS(stripComment(line));
        if (!t.empty())
        {
            firstToken = t[0];
            break;
        }
    }
    probe.close();

    if (firstToken == "@HEADER")
        return FTilemapParser::Parse(filepath, doc);

    std::cout << "[UTilemap] Warning: legacy .tilemap format. "
                 "Consider converting to the new @HEADER/@SYMBOLS/@DEFS/@INSTANCES format.\n";
    return FTilemapParser_Legacy::Parse(filepath, doc);
}

UPlayerCharacter* UTilemap::Spawn(UScene& scene) const
{
    return UTilemapSpawner::Spawn(doc, scene);
}
