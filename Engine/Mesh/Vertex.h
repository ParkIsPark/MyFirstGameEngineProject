#pragma once
#include <glm/glm.hpp>

// Single vertex format shared by the rasterizer, the ray tracer, and every
// importer (OBJ / FBX). Keeping one POD layout means no conversion between
// stages. sizeof == 32 bytes (GPU-friendly).
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};
