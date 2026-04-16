#pragma once
#include <glm/glm.hpp>
#include <string>

class AActor;
class UScene;
class ACamera;
class URayTracing;
struct URay;

// ---------------------------------------------------------------------------
// GPU code-generation descriptor — returned by each LightComponent subclass.
// LightingPass collects these to build the lighting functions and the shade()
// dispatcher dynamically at shader-assembly time.
// ---------------------------------------------------------------------------
struct LightGLSLInfo
{
    std::string constants;        // const float SOFT_OX[] / GOLDEN_ANGLE etc.
    std::string uniforms;         // uniform declarations
    std::string functions;        // shadePointLight / shadeEnvLight etc.
    std::string directContrib;    // lines added to shadeDirect() body
    std::string indirectContrib;  // lines added to shadeIndirect() body
};

class LightComponent
{
public:
    glm::vec3 LightColor;
    glm::vec3 LightIntensity;

    LightComponent(glm::vec3 color = glm::vec3(1.0f), glm::vec3 intensity = glm::vec3(1.0f));
    virtual ~LightComponent() = default;

    virtual glm::vec3 illuminate(
        const glm::vec3&    hitPoint,
        const glm::vec3&    normal,
        const AActor*       actor,
        const URay&         ray,
        const UScene&       scene,
        const ACamera&      camera,
        int                 depth,
        const URayTracing*  tracer) const = 0;

    // Returns GPU code-generation info for this light type.
    // Called once at shader-assembly time (URayTracing::Init).
    virtual LightGLSLInfo getGLSLInfo() const = 0;
};
