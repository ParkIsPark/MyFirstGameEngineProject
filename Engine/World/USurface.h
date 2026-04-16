#pragma once
#include "URay.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// GPU code-generation descriptor — returned by each Surface subclass.
// IntersectionPass collects these at shader-assembly time to build the
// intersection / normal / shading dispatch functions dynamically.
// ---------------------------------------------------------------------------
struct SurfaceGLSLInfo
{
    int         typeId;         // 0 = sphere, 1 = cube, 2 = plane
    std::string uniforms;       // GLSL uniform-array declarations for this type
    std::string hitFunc;        // float xxxHit(vec3 ro, vec3 rd, int i) { ... }
    std::string hitFuncName;    // "sphHit" / "cubeHit" / "planeHit"
    std::string countUniform;   // "uNSph" / "uNCube" / "uNPlane"
    std::string normalCase;     // one `if (type == N)` branch for getNormal()
    std::string diffuseCase;    // one `if (type == N)` branch for getDiffuse()
    std::string kaCase;         // one `if (t == N)` branch for getKa()
    std::string ksCase;
    std::string shinyCase;
    std::string kmCase;
    std::string emitCase;       // empty for non-emitting surfaces
};

class AActor; // forward declaration — avoids circular include with AActor.h

// -----------------------------------------------------------------------
// Phong ray tracing material coefficients
//   L = ka*Ia + km*L_refl + sum_lights[ kd*(N.L) + ks*(N.H)^p ] * shadow
// -----------------------------------------------------------------------
struct Material
{
    glm::vec3    ka        = glm::vec3(0.2f); // ambient  : constant fill light, prevents fully black shadowed areas
    glm::vec3    kd        = glm::vec3(1.0f); // diffuse  : Lambertian reflection, gives the object its base color
    glm::vec3    ks        = glm::vec3(0.0f); // specular : Blinn-Phong highlight color (usually white/grey for metals)
    float        shininess = 0.0f;            // phong exp: controls highlight size — higher = sharper, smaller = broader
    glm::vec3    km        = glm::vec3(0.0f); // mirror   : reflectance for recursive reflection rays; 0=matte, 1=perfect mirror
    glm::vec3    emissive  = glm::vec3(0.0f); // emissive : light emitted by the surface itself; propagates through GI bounces
    unsigned int texture   = 0;               // OpenGL texture object ID (0 = no texture)

    // CPU-side raw pixel data for ray tracer texture sampling
    std::vector<unsigned char> texData;
    int texWidth    = 0;
    int texHeight   = 0;
    int texChannels = 0;
};

class USurface
{
public:
    virtual ~USurface() {}

    Material  material;
    AActor*   owner = nullptr; // set by AActor::SetSurface(); used to read position

    // Returns owner->position. Implemented in USurface.cpp to access full AActor definition.
    virtual glm::vec3 getPosition() const;

    // Returns true if ray hits this surface at parameter t > 0.
    // On hit, writes the smallest positive t into tHit.
    virtual bool intersect(const URay& ray, float& tHit) const = 0;

    // Returns the outward surface normal at the given hit point.
    virtual glm::vec3 getNormal(const glm::vec3& hitPoint) const = 0;

    // Convenience setter: assigns TargetColor to kd (and optionally ka).
    virtual void SetColor(const glm::vec3& TargetColor) = 0;

    // Loads an image from disk, uploads it to OpenGL, and stores the ID in texture.
    void SetTexture(const char* filePath);

    // Returns UV coordinates [0,1] for the given hit point (used for texture mapping).
    virtual glm::vec2 getUV(const glm::vec3& hitPoint) const = 0;

    // Returns texture sample at hitPoint's UV if a texture is loaded, otherwise returns kd.
    glm::vec3 getDiffuseColor(const glm::vec3& hitPoint) const;

    // Returns GPU code-generation info for this surface type.
    // Called once at shader-assembly time (URayTracing::Init).
    virtual SurfaceGLSLInfo getGLSLInfo() const = 0;

    // Uploads this surface's data to the GPU uniform arrays at the given index.
    // Called every frame by IntersectionPass::uploadUniforms().
    virtual void uploadUniform(unsigned int prog, int idx) const = 0;
};
