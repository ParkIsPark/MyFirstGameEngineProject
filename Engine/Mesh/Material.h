#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>

// UV addressing for the diffuse texture. GL sampler wrap is texture-wide, so
// per-material wrap is applied in SampleDiffuse / the shader (P3 decision).
enum class EWrapMode { Repeat, Clamp };

// ---------------------------------------------------------------------------
// Phong/Blinn-Phong material coefficients.  Owned by UMesh (per-asset default)
// and overridable per-instance on UMeshComponent.  Moved out of the deleted
// USurface during the mesh-first legacy purge (struct reused verbatim).
//   L = ka*Ia + km*L_refl + sum_lights[ kd*(N.L) + ks*(N.H)^p ] * shadow
// ---------------------------------------------------------------------------
struct Material
{
    glm::vec3    ka        = glm::vec3(0.2f); // ambient  : constant fill light, prevents fully black shadowed areas
    glm::vec3    kd        = glm::vec3(1.0f); // diffuse  : Lambertian reflection, gives the object its base color
    glm::vec3    ks        = glm::vec3(0.0f); // specular : Blinn-Phong highlight color (usually white/grey for metals)
    float        shininess = 0.0f;            // phong exp: controls highlight size — higher = sharper, smaller = broader
    glm::vec3    km        = glm::vec3(0.0f); // mirror   : reflectance for recursive reflection rays; 0=matte, 1=perfect mirror
    glm::vec3    emissive  = glm::vec3(0.0f); // emissive : light emitted by the surface itself; propagates through GI bounces
    unsigned int texture   = 0;               // OpenGL texture object ID (0 = no texture)

    // CPU-side raw pixel data for ray tracer / sampler texture lookup
    std::vector<unsigned char> texData;
    int texWidth    = 0;
    int texHeight   = 0;
    int texChannels = 0;

    // ---- diffuse texture (P3) ----
    std::string diffuseTexPath;                 // Content-relative; empty = untextured
    EWrapMode   wrapMode = EWrapMode::Repeat;   // per-material UV addressing
    glm::vec2   uvTiling = glm::vec2(1.0f);     // UV scale before wrap

    // Bidirectional serialization (numeric Blinn-Phong + diffuse-texture fields).
    void Serialize(class FArchive& ar);

    // CPU diffuse sample at uv (applies uvTiling + wrapMode, sRGB->linear).
    // Returns kd when there is no CPU texture data.
    glm::vec3 SampleDiffuse(glm::vec2 uv) const;
};
