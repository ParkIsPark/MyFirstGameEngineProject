#include "UFbxImporter.h"
#include "UMesh.h"

#include <iostream>
#include <utility>
#include <glm/glm.hpp>

#define ASSIMP_DLL              // consume Assimp as a DLL (ASSIMP_API = dllimport)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

namespace
{
    void ConvertMaterial(const aiMaterial* am, Material& out)
    {
        aiColor3D c;
        if (am->Get(AI_MATKEY_COLOR_AMBIENT,  c) == AI_SUCCESS) out.ka = glm::vec3(c.r, c.g, c.b);
        if (am->Get(AI_MATKEY_COLOR_DIFFUSE,  c) == AI_SUCCESS) out.kd = glm::vec3(c.r, c.g, c.b);
        if (am->Get(AI_MATKEY_COLOR_SPECULAR, c) == AI_SUCCESS) out.ks = glm::vec3(c.r, c.g, c.b);
        float shin = 0.0f;
        if (am->Get(AI_MATKEY_SHININESS, shin) == AI_SUCCESS) out.shininess = shin;
        // map_Kd texture path resolution / stb_image upload is a later concern.
    }

    UMesh* ConvertMesh(const aiMesh* m, const aiScene* sc,
                       const UFbxImporter::LoadOptions& opt)
    {
        UMesh* mesh = new UMesh();
        mesh->vertices.reserve(m->mNumVertices);

        const bool hasUV = m->mTextureCoords[0] != nullptr;
        for (unsigned v = 0; v < m->mNumVertices; ++v)
        {
            Vertex vx;
            vx.position = glm::vec3(m->mVertices[v].x, m->mVertices[v].y, m->mVertices[v].z) * opt.globalScale;
            if (opt.swapYZ) { std::swap(vx.position.y, vx.position.z); vx.position.z = -vx.position.z; }

            vx.normal = m->HasNormals()
                ? glm::vec3(m->mNormals[v].x, m->mNormals[v].y, m->mNormals[v].z)
                : glm::vec3(0, 1, 0);
            if (opt.swapYZ) { std::swap(vx.normal.y, vx.normal.z); vx.normal.z = -vx.normal.z; }

            vx.uv = hasUV
                ? glm::vec2(m->mTextureCoords[0][v].x, m->mTextureCoords[0][v].y)
                : glm::vec2(0.0f);

            mesh->vertices.push_back(vx);
        }

        mesh->indices.reserve(static_cast<size_t>(m->mNumFaces) * 3);
        for (unsigned f = 0; f < m->mNumFaces; ++f)
        {
            const aiFace& face = m->mFaces[f];
            if (face.mNumIndices != 3) continue;          // post-Triangulate: always 3
            mesh->indices.push_back(face.mIndices[0]);
            mesh->indices.push_back(face.mIndices[1]);
            mesh->indices.push_back(face.mIndices[2]);
        }

        if (m->mMaterialIndex < sc->mNumMaterials)
            ConvertMaterial(sc->mMaterials[m->mMaterialIndex], mesh->material);

        return mesh;
    }
}

std::vector<UMesh*> UFbxImporter::Load(const char* path, const LoadOptions& opt)
{
    if (!path) return {};

    Assimp::Importer importer;
    unsigned flags = aiProcess_Triangulate
                   | aiProcess_GenSmoothNormals
                   | aiProcess_JoinIdenticalVertices
                   | aiProcess_GlobalScale;
    if (opt.flipUV) flags |= aiProcess_FlipUVs;

    const aiScene* sc = importer.ReadFile(path, flags);
    if (!sc || (sc->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !sc->mRootNode)
    {
        std::cerr << "UFbxImporter: failed to load '" << path << "': "
                  << importer.GetErrorString() << "\n";
        return {};
    }

    std::vector<UMesh*> out;
    out.reserve(sc->mNumMeshes);
    for (unsigned i = 0; i < sc->mNumMeshes; ++i)
        out.push_back(ConvertMesh(sc->mMeshes[i], sc, opt));
    return out;
}
