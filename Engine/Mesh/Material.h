#pragma once
#include <glm/glm.hpp>
#include <vector>

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
};
