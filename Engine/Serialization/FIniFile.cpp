#include "FIniFile.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }
}

bool FIniFile::LoadFromFile(const char* path)
{
    if (!path || !*path) return false;
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss; ss << f.rdbuf();
    Parse(ss.str());
    return true;
}

void FIniFile::Parse(const std::string& text)
{
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line))
    {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::string t = trim(line);
        if (t.empty()) continue;

        if (t.front() == '[' && t.back() == ']')
        {
            section = lower(trim(t.substr(1, t.size() - 2)));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(t.substr(0, eq)));
        std::string val = trim(t.substr(eq + 1));
        if (!key.empty()) data_[section][key] = val;
    }
}

bool FIniFile::Has(const std::string& section, const std::string& key) const
{
    auto s = data_.find(lower(section));
    if (s == data_.end()) return false;
    return s->second.find(lower(key)) != s->second.end();
}

std::string FIniFile::GetString(const std::string& section, const std::string& key, const std::string& def) const
{
    auto s = data_.find(lower(section));
    if (s == data_.end()) return def;
    auto k = s->second.find(lower(key));
    return k == s->second.end() ? def : k->second;
}

int FIniFile::GetInt(const std::string& section, const std::string& key, int def) const
{
    if (!Has(section, key)) return def;
    try { return std::stoi(GetString(section, key)); } catch (...) { return def; }
}

float FIniFile::GetFloat(const std::string& section, const std::string& key, float def) const
{
    if (!Has(section, key)) return def;
    try { return std::stof(GetString(section, key)); } catch (...) { return def; }
}

bool FIniFile::GetBool(const std::string& section, const std::string& key, bool def) const
{
    if (!Has(section, key)) return def;
    const std::string v = lower(GetString(section, key));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

glm::vec3 FIniFile::GetVec3(const std::string& section, const std::string& key, const glm::vec3& def) const
{
    if (!Has(section, key)) return def;
    std::istringstream is(GetString(section, key));
    glm::vec3 v = def;
    is >> v.x >> v.y >> v.z;
    return v;
}
