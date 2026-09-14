#include "FRenderMath.h"
#include "FRenderQuality.h"

#include <cassert>
#include <cmath>

namespace
{
bool Near(float a, float b, float epsilon = 0.0001f)
{
    return std::fabs(a - b) <= epsilon;
}

bool NearVec(const glm::vec3& a, const glm::vec3& b, float epsilon)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) &&
           Near(a.z, b.z, epsilon);
}
}

int main()
{
    FRenderQuality invalid;
    invalid.ssaa = 7;
    invalid.shadowSamples = 0;
    invalid.giSamples = 99;
    invalid.giBounces = -2;
    invalid.temporalFrames = 1000;
    invalid.anisotropy = -1.0f;
    const FRenderQuality q = SanitizeRenderQuality(invalid);
    assert(q.ssaa == 2);
    assert(q.shadowSamples == 1);
    assert(q.giSamples == 32);
    assert(q.giBounces == 0);
    assert(q.temporalFrames == 32);
    assert(q.anisotropy == 1.0f);
    assert(Near(PointLightAttenuation(1.0f), 1.0f));
    assert(Near(PointLightAttenuation(4.0f), 0.25f));
    assert(Near(PointLightAttenuation(16.0f), 0.0625f));
    assert(Near(SchlickFresnel(1.0f, 1.0f, 1.5f), 0.04f, 0.001f));
    assert(NearVec(BeerLambertFromTransmittance(
                       {0.5f, 0.25f, 1.0f}, 2.0f, 2.0f),
                   {0.5f, 0.25f, 1.0f}, 0.001f));
    assert(ACESFitted({4.0f, 2.0f, 1.0f}).r <= 1.0f);
    return 0;
}
