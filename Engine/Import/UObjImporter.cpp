#include "UObjImporter.h"
#include "UMesh.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Face-token parser. Walks the token character by character so it handles every
// "v", "v/vt", "v//vn", "v/vt/vn" form plus negative indices without splitting.
// ---------------------------------------------------------------------------
UObjImporter::IndexTriple
UObjImporter::ParseFaceToken(const std::string& tok, int np, int nt, int nn)
{
    int  parts[3] = { 0, 0, 0 };
    bool has[3]   = { false, false, false };
    int  field = 0, sign = 1, val = 0;
    bool reading = false;

    for (char c : tok)
    {
        if (c == '/')
        {
            if (reading) { parts[field] = sign * val; has[field] = true; }
            if (field < 2) ++field;
            sign = 1; val = 0; reading = false;
        }
        else if (c == '-') { sign = -1; }
        else if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); reading = true; }
    }
    if (reading) { parts[field] = sign * val; has[field] = true; }

    auto resolve = [](int idx, int size) -> int
    {
        if (idx == 0) return -1;
        return (idx > 0) ? (idx - 1) : (size + idx);   // negative = from the end
    };

    IndexTriple r;
    r.p = has[0] ? resolve(parts[0], np) : -1;
    r.t = has[1] ? resolve(parts[1], nt) : -1;
    r.n = has[2] ? resolve(parts[2], nn) : -1;
    return r;
}

namespace
{
    struct TripleHash
    {
        size_t operator()(const UObjImporter::IndexTriple& k) const
        {
            // mix the three indices (cheap 64-bit-ish hash)
            size_t h = static_cast<size_t>(k.p) * 73856093u;
            h ^= static_cast<size_t>(k.t) * 19349663u;
            h ^= static_cast<size_t>(k.n) * 83492791u;
            return h;
        }
    };
    struct TripleEq
    {
        bool operator()(const UObjImporter::IndexTriple& a,
                        const UObjImporter::IndexTriple& b) const
        {
            return a.p == b.p && a.t == b.t && a.n == b.n;
        }
    };

    void ComputeMissingNormals(UMesh& m)
    {
        std::vector<glm::vec3> accum(m.vertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        {
            const uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            const glm::vec3 e1 = m.vertices[b].position - m.vertices[a].position;
            const glm::vec3 e2 = m.vertices[c].position - m.vertices[a].position;
            const glm::vec3 fn = glm::cross(e1, e2);
            accum[a] += fn; accum[b] += fn; accum[c] += fn;
        }
        for (size_t i = 0; i < m.vertices.size(); ++i)
            m.vertices[i].normal = (glm::length(accum[i]) > 1e-8f)
                ? glm::normalize(accum[i]) : glm::vec3(0, 1, 0);
    }
}

UMesh* UObjImporter::Load(const char* path)
{
    if (!path) return nullptr;
    std::ifstream f(path);
    if (!f.is_open()) return nullptr;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    UMesh* mesh = new UMesh();
    std::unordered_map<IndexTriple, uint32_t, TripleHash, TripleEq> cache;
    bool anyNormalProvided = false;

    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "v")
        {
            glm::vec3 p(0.0f); iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (tag == "vt")
        {
            glm::vec2 t(0.0f); iss >> t.x >> t.y;
            uvs.push_back(t);
        }
        else if (tag == "vn")
        {
            glm::vec3 n(0.0f); iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (tag == "f")
        {
            std::vector<uint32_t> faceIdx;
            std::string tok;
            while (iss >> tok)
            {
                const IndexTriple it = ParseFaceToken(
                    tok, static_cast<int>(positions.size()),
                         static_cast<int>(uvs.size()),
                         static_cast<int>(normals.size()));
                if (it.p < 0 || it.p >= static_cast<int>(positions.size()))
                    continue;                                    // skip malformed vertex

                auto found = cache.find(it);
                uint32_t vidx;
                if (found != cache.end())
                {
                    vidx = found->second;
                }
                else
                {
                    Vertex v;
                    v.position = positions[it.p];
                    v.uv       = (it.t >= 0 && it.t < (int)uvs.size())     ? uvs[it.t]     : glm::vec2(0.0f);
                    v.normal   = (it.n >= 0 && it.n < (int)normals.size()) ? normals[it.n] : glm::vec3(0.0f);
                    if (it.n >= 0) anyNormalProvided = true;
                    vidx = static_cast<uint32_t>(mesh->vertices.size());
                    mesh->vertices.push_back(v);
                    cache[it] = vidx;
                }
                faceIdx.push_back(vidx);
            }
            // fan triangulation: (0,1,2), (0,2,3), (0,3,4), ...
            for (size_t i = 1; i + 1 < faceIdx.size(); ++i)
            {
                mesh->indices.push_back(faceIdx[0]);
                mesh->indices.push_back(faceIdx[i]);
                mesh->indices.push_back(faceIdx[i + 1]);
            }
        }
        // mtllib / usemtl / o / g / s : ignored in this phase
    }

    if (!anyNormalProvided)
        ComputeMissingNormals(*mesh);

    return mesh;
}
