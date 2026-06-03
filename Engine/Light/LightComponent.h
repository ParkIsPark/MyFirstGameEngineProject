#pragma once
#include <glm/glm.hpp>
#include <string>

// ---------------------------------------------------------------------------
// GPU code-generation descriptor — returned by each LightComponent subclass.
// Carries the per-light-type GLSL snippets (constants/uniforms/functions and
// the direct/indirect contribution lines) used to assemble the lighting code.
// The old multi-pass shader assembler that consumed this was removed with the
// legacy ray tracer; the descriptor is retained as the foundation for feeding
// light parameters into the GPU shading path (future editor light controls).
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

    // Returns GPU code-generation info for this light type.
    // (The CPU illuminate() path was removed with the CPU ray tracer; lighting
    //  is GPU-only now. This descriptor feeds the GPU shader assembler.)
    virtual LightGLSLInfo getGLSLInfo() const = 0;
};
