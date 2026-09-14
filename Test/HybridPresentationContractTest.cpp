// Focused contract coverage for the shared HDR composition/presentation path.
#include "FRenderMath.h"
#include "FRenderQuality.h"
#include "Shaders/HybridPresentationShaders.h"
#include "UHybridPresentationPass.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
bool Near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001f;
}

std::size_t CountOccurrences(const std::string& text, const char* needle)
{
    std::size_t count = 0;
    for (std::size_t offset = 0;
         (offset = text.find(needle, offset)) != std::string::npos;
         offset += std::char_traits<char>::length(needle))
        ++count;
    return count;
}

void CheckInternalRenderSize()
{
    const FInternalRenderSize one = ResolveInternalRenderSize(800, 450, 1, 4096);
    assert(one.width == 800 && one.height == 450);
    const FInternalRenderSize two = ResolveInternalRenderSize(800, 450, 2, 4096);
    assert(two.width == 1600 && two.height == 900);

    assert(ResolveInternalRenderSize(0, 450, 1, 4096).width == 0);
    assert(ResolveInternalRenderSize(800, 450, 3, 4096).width == 0);
    assert(ResolveInternalRenderSize(3000, 450, 2, 4096).width == 0);
    assert(ResolveInternalRenderSize(0x40000000, 2, 2, 0x7fffffff).width == 0);
}

void CheckOpticalWeightsRemainEnergyBounded()
{
    const FMaterialOpticalWeights mirror = ResolveMaterialOpticalWeights(
        false, 1.0f, 1.0f, 1.0f, true);
    assert(Near(mirror.local, 0.0f));
    assert(Near(mirror.mirror, 1.0f));
    assert(Near(mirror.transmission, 0.0f));

    const FMaterialOpticalWeights glass = ResolveMaterialOpticalWeights(
        true, 0.0f, 1.0f, 1.0f, true);
    assert(Near(glass.local, 0.0f));
    assert(Near(glass.mirror, 0.0f));
    assert(Near(glass.transmission, 1.0f));
}

void CheckACESMonotonicity()
{
    const glm::vec3 low = ACESFitted(glm::vec3(0.25f));
    const glm::vec3 middle = ACESFitted(glm::vec3(1.0f));
    const glm::vec3 high = ACESFitted(glm::vec3(4.0f));
    assert(low.r < middle.r && middle.r < high.r);
    assert(high.r < 1.0f && high.r > 0.9f);
}

void CheckSingleDisplayConversion()
{
    const std::string composite = HybridPresentationShaders::CompositeFragment;
    const std::string presentation = HybridPresentationShaders::PresentationFragment;
    assert(composite.find("linearToSRGB") == std::string::npos);
    assert(composite.find("pow(") == std::string::npos);
    assert(composite.find("max(") == std::string::npos);
    assert(composite.find("clamp(") == std::string::npos);
    assert(CountOccurrences(presentation, "vec3 linearToSRGB(") == 1);
    assert(CountOccurrences(presentation, "linearToSRGB(mapped)") == 1);
    assert(CountOccurrences(presentation, "max(") == 1);
    assert(CountOccurrences(presentation, "clamp(") == 1);
    assert(presentation.find(
        "vec3 linearColor = texture(uHDRInput, vUV).rgb;") != std::string::npos);
}
} // namespace

int main()
{
    CheckInternalRenderSize();
    CheckOpticalWeightsRemainEnergyBounded();
    CheckACESMonotonicity();
    CheckSingleDisplayConversion();
    std::cout << "HybridPresentationContractTest passed\n";
    return 0;
}
