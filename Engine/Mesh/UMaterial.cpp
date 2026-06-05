#include "UMaterial.h"
#include "Material.h"
#include "FArchive.h"
#include "stb_image.h"          // declarations only; impl lives in USkyHDRI.cpp

#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cctype>

namespace
{
    std::string lowerExt(const std::string& path)
    {
        const size_t dot = path.find_last_of('.');
        std::string e = (dot == std::string::npos) ? "" : path.substr(dot);
        for (char& c : e) c = (char)std::tolower((unsigned char)c);
        return e;
    }

    std::string dirOf(const std::string& path)
    {
        const size_t s = path.find_last_of("/\\");
        return (s == std::string::npos) ? "" : path.substr(0, s + 1);
    }

    // Minimal Wavefront .mtl reader: first material's Kd/Ks/Ns/map_Kd. map_Kd is
    // resolved relative to the .mtl's folder so a Content-copied OBJ resolves.
    void parseMtl(const std::string& path, Material& m)
    {
        std::ifstream f(path);
        if (!f) return;
        const std::string dir = dirOf(path);
        std::string line; bool seen = false;
        while (std::getline(f, line))
        {
            std::istringstream ss(line);
            std::string tok; ss >> tok;
            if (tok == "newmtl") { if (seen) break; seen = true; }
            else if (tok == "Kd") { ss >> m.kd.x >> m.kd.y >> m.kd.z; }
            else if (tok == "Ks") { ss >> m.ks.x >> m.ks.y >> m.ks.z; }
            else if (tok == "Ka") { ss >> m.ka.x >> m.ka.y >> m.ka.z; }
            else if (tok == "Ns") { ss >> m.shininess; }
            else if (tok == "map_Kd")
            {
                std::string name; std::getline(ss, name);
                const size_t a = name.find_first_not_of(" \t");
                if (a != std::string::npos) m.diffuseTexPath = dir + name.substr(a);
            }
        }
    }
}

void UMaterial::LoadTexture(Material& m)
{
    m.texData.clear(); m.texWidth = m.texHeight = m.texChannels = 0;
    if (m.diffuseTexPath.empty()) return;
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* d = stbi_load(m.diffuseTexPath.c_str(), &w, &h, &n, 0);
    if (!d) return;
    m.texData.assign(d, d + (size_t)w * h * n);
    m.texWidth = w; m.texHeight = h; m.texChannels = n;
    stbi_image_free(d);
}

Material* UMaterial::Resolve(const std::string& path)
{
    static std::unordered_map<std::string, Material*> cache;
    if (path.empty()) return nullptr;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    Material* m = new Material();
    const std::string ext = lowerExt(path);
    if (ext == ".mtl")
    {
        parseMtl(path, *m);
    }
    else   // .material (or anything else) -> FArchive load, robust to missing/garbage
    {
        std::ifstream f(path);
        if (f) { std::stringstream ss; ss << f.rdbuf(); FLoadArchive ar(ss.str()); m->Serialize(ar); }
    }
    LoadTexture(*m);
    cache[path] = m;
    return m;
}

bool UMaterial::Save(const std::string& path, const Material& m)
{
    std::ofstream f(path);
    if (!f) return false;
    FSaveArchive ar;
    const_cast<Material&>(m).Serialize(ar);    // Serialize is read/write; saving here
    f << "MaterialFormat = 1\n" << ar.str();
    if (!f) return false;

    // Keep the shared cache instance in sync with what we just wrote, so the
    // material editor's live edits and the on-disk file never diverge.
    if (Material* shared = Resolve(path); shared && shared != &m)
    {
        *shared = m;
        LoadTexture(*shared);
    }
    return true;
}
