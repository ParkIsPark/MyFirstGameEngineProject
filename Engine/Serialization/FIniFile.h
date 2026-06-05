#pragma once
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// FIniFile (P7) — robust sectioned INI parser.
//
//   [Section]
//   Key = Value        # comment
//
// Sections/keys are case-insensitive. Missing file / garbage lines never throw
// (FProjectDescriptor robustness contract): unknown lookups return the default.
// Shared by Config/Engine.ini, Config/EditorSettings.ini, and the project's
// Setting/Default*.ini.
// ---------------------------------------------------------------------------
class FIniFile
{
public:
    bool LoadFromFile(const char* path);     // false if missing/unreadable
    void Parse(const std::string& text);

    bool        Has   (const std::string& section, const std::string& key) const;
    std::string GetString(const std::string& section, const std::string& key, const std::string& def = "") const;
    int         GetInt   (const std::string& section, const std::string& key, int   def = 0)   const;
    float       GetFloat (const std::string& section, const std::string& key, float def = 0.f) const;
    bool        GetBool  (const std::string& section, const std::string& key, bool  def = false) const;
    glm::vec3   GetVec3  (const std::string& section, const std::string& key, const glm::vec3& def = glm::vec3(0.0f)) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
};
