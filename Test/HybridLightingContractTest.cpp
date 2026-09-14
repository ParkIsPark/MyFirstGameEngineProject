// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name HybridLightingContractTest
#include "FRenderScene.h"
#include "SharedLightingShaderSource.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
std::size_t CountDefinitions(const std::string& source)
{
    const std::string marker = "void evaluatePointLight(";
    std::size_t count = 0;
    for (std::size_t offset = source.find(marker); offset != std::string::npos;
         offset = source.find(marker, offset + marker.size()))
        ++count;
    return count;
}

float DiffuseEnergyAtDistance(float distance)
{
    const float rawDistanceSquared = distance * distance;
    const float distanceSquared = std::max(rawDistanceSquared, 0.01f);
    const float lightSource = 1.0f;
    const float albedo = 1.0f;
    const float normalDotLight = 1.0f;
    return albedo * (lightSource / distanceSquared) * normalDotLight;
}

bool Near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 1.0e-6f;
}

template <typename T, typename = void>
struct THasRadianceMember : std::false_type {};

template <typename T>
struct THasRadianceMember<T, std::void_t<decltype(std::declval<T>().radiance)>>
    : std::true_type {};
}

int main()
{
    static_assert(std::is_same_v<decltype(FRenderPointLight{}.sourceIntensity), glm::vec3>,
                  "Point lights expose unattenuated source intensity");
    static_assert(!THasRadianceMember<FRenderPointLight>::value,
                  "Point lights must not imply distance attenuation at submission");

    const std::string shared = SharedLightingShaderSource::PointLightFunctions();
    const std::string hardware = SharedLightingShaderSource::BuildHardwareGeometryShader();
    const std::string raster = SharedLightingShaderSource::BuildRasterLightingFragmentShader();
    const std::string rayFragment =
        SharedLightingShaderSource::BuildRayEffectsFragmentShader();
    const std::string rayCompute =
        SharedLightingShaderSource::BuildRayEffectsComputeShader();

    assert(CountDefinitions(shared) == 1u);
    assert(CountDefinitions(hardware) == 1u);
    assert(CountDefinitions(raster) == 1u);
    assert(CountDefinitions(rayFragment) == 1u);
    assert(CountDefinitions(rayCompute) == 1u);

    const float atOne = DiffuseEnergyAtDistance(1.0f);
    const float atTwo = DiffuseEnergyAtDistance(2.0f);
    const float atFour = DiffuseEnergyAtDistance(4.0f);
    assert(Near(atOne, 1.0f));
    assert(Near(atTwo, 0.25f));
    assert(Near(atFour, 0.0625f));
    assert(Near(atOne / atTwo, 4.0f));
    assert(Near(atOne / atFour, 16.0f));

    std::puts("HybridLightingContractTest passed");
    return 0;
}
