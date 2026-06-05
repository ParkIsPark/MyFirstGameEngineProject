#include "FArchive.h"

#include <sstream>
#include <iomanip>

// ---- formatting helpers ---------------------------------------------------
static std::string f2s(float v)
{
    std::ostringstream os;
    os << std::setprecision(9) << v;       // round-trip-safe for float
    return os.str();
}
static std::string v2s(const glm::vec3& v)
{
    return f2s(v.x) + " " + f2s(v.y) + " " + f2s(v.z);
}
static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---- FSaveArchive ---------------------------------------------------------
void FSaveArchive::Field(const char* key, float& v)       { out_ += key; out_ += " = " + f2s(v) + "\n"; }
void FSaveArchive::Field(const char* key, int& v)         { out_ += key; out_ += " = " + std::to_string(v) + "\n"; }
void FSaveArchive::Field(const char* key, bool& v)        { out_ += key; out_ += " = "; out_ += (v ? "1" : "0"); out_ += "\n"; }
void FSaveArchive::Field(const char* key, glm::vec2& v)   { out_ += key; out_ += " = " + f2s(v.x) + " " + f2s(v.y) + "\n"; }
void FSaveArchive::Field(const char* key, glm::vec3& v)   { out_ += key; out_ += " = " + v2s(v) + "\n"; }
void FSaveArchive::Field(const char* key, std::string& v) { out_ += key; out_ += " = " + v + "\n"; }

// ---- FLoadArchive ---------------------------------------------------------
FLoadArchive::FLoadArchive(const std::string& block)
{
    std::istringstream in(block);
    std::string line;
    while (std::getline(in, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;        // skip non key=value lines
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty() || key[0] == '#') continue;   // skip comments / blanks
        kv_[key] = val;
    }
}

void FLoadArchive::Field(const char* key, float& v)
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return;
    std::istringstream is(it->second); float t; if (is >> t) v = t;
}
void FLoadArchive::Field(const char* key, int& v)
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return;
    std::istringstream is(it->second); int t; if (is >> t) v = t;
}
void FLoadArchive::Field(const char* key, bool& v)
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return;
    const std::string& s = it->second;
    v = (s == "1" || s == "true" || s == "True");
}
void FLoadArchive::Field(const char* key, glm::vec2& v)
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return;
    std::istringstream is(it->second);
    glm::vec2 t = v;
    is >> t.x >> t.y;
    v = t;
}
void FLoadArchive::Field(const char* key, glm::vec3& v)
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return;
    std::istringstream is(it->second);
    glm::vec3 t = v;
    is >> t.x >> t.y >> t.z;        // partial parse leaves untouched components
    v = t;
}
void FLoadArchive::Field(const char* key, std::string& v)
{
    auto it = kv_.find(key);
    if (it != kv_.end()) v = it->second;
}
