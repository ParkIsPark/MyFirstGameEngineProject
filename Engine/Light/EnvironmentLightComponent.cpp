#include "EnvironmentLightComponent.h"
#include "FArchive.h"

#include <algorithm>

REGISTER_COMPONENT("EnvLight", EnvironmentLightComponent)

EnvironmentLightComponent::EnvironmentLightComponent()
    : LightComponent(glm::vec3(1.0f), glm::vec3(1.0f))
{
}

EnvironmentLightComponent::EnvironmentLightComponent(glm::vec3 color, glm::vec3 intensity)
    : LightComponent(color, intensity)
{
}

void EnvironmentLightComponent::Serialize(FArchive& ar)
{
    LightComponent::Serialize(ar);
    ar.Color("Horizon", horizonColor);
    ar.Color("Zenith",  zenithColor);
    ar.Field("SkyExp",  skyExp);
    ar.Field("SkyTex",  skyTexPath);   // actor-placed HDRI image (Content path)
    ar.Field("TimeOfDay", timeOfDay);  // sun position (hour 0..24); slider re-applies on edit
}

// ---------------------------------------------------------------------------
//  applyTimeOfDay — sets sky gradient + light color/intensity from the sun's
//  position over a full day. hour in [0,24]: 0/24 midnight, ~7 sunrise,
//  12 noon, ~17 sunset, ~19 dusk. Keyframes are interpolated linearly.
// ---------------------------------------------------------------------------
void EnvironmentLightComponent::applyTimeOfDay(EnvironmentLightComponent& light, float hour)
{
    hour = glm::clamp(hour, 0.0f, 24.0f);

    struct Key {
        float     t;
        glm::vec3 horizon;
        glm::vec3 zenith;
        float     skyExp;
        float     intensity;
        glm::vec3 lightColor;
    };

    static const Key keys[] = {
        {  0.0f, glm::vec3(0.02f,0.02f,0.08f), glm::vec3(0.00f,0.00f,0.05f), 1.5f, 0.00f, glm::vec3(0.50f,0.55f,0.80f) }, // midnight
        {  5.0f, glm::vec3(0.30f,0.15f,0.25f), glm::vec3(0.05f,0.05f,0.20f), 1.2f, 0.06f, glm::vec3(0.70f,0.60f,0.80f) }, // pre-dawn
        {  7.0f, glm::vec3(0.95f,0.55f,0.25f), glm::vec3(0.35f,0.55f,0.85f), 0.8f, 0.30f, glm::vec3(1.00f,0.80f,0.55f) }, // sunrise
        { 12.0f, glm::vec3(0.95f,0.92f,0.82f), glm::vec3(0.25f,0.55f,1.00f), 0.6f, 1.00f, glm::vec3(1.00f,0.97f,0.90f) }, // noon
        { 17.0f, glm::vec3(0.97f,0.55f,0.30f), glm::vec3(0.30f,0.45f,0.85f), 0.8f, 0.35f, glm::vec3(1.00f,0.75f,0.50f) }, // sunset
        { 19.0f, glm::vec3(0.30f,0.15f,0.25f), glm::vec3(0.05f,0.05f,0.20f), 1.2f, 0.08f, glm::vec3(0.70f,0.60f,0.80f) }, // dusk
        { 24.0f, glm::vec3(0.02f,0.02f,0.08f), glm::vec3(0.00f,0.00f,0.05f), 1.5f, 0.00f, glm::vec3(0.50f,0.55f,0.80f) }, // midnight
    };
    constexpr int N = 7;

    const float tod = hour;

    int lo = 0;
    for (int i = 0; i < N - 1; ++i)
        if (tod >= keys[i].t) lo = i;
    int hi = std::min(lo + 1, N - 1);

    float alpha = (hi == lo) ? 0.0f : (tod - keys[lo].t) / (keys[hi].t - keys[lo].t);

    light.horizonColor   = glm::mix(keys[lo].horizon,    keys[hi].horizon,    alpha);
    light.zenithColor    = glm::mix(keys[lo].zenith,     keys[hi].zenith,     alpha);
    light.skyExp         = glm::mix(keys[lo].skyExp,     keys[hi].skyExp,     alpha);
    light.LightIntensity = glm::vec3(glm::mix(keys[lo].intensity, keys[hi].intensity, alpha));
    light.LightColor     = glm::mix(keys[lo].lightColor, keys[hi].lightColor, alpha);
}
