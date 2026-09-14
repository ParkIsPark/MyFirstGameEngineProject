#include "FRenderQualitySettings.h"

#include "FIniFile.h"
#include "FRenderMath.h"

#include <ostream>

FRenderQuality ReadRenderQuality(
    const FIniFile& ini, const char* section, const FRenderQuality& defaults)
{
    FRenderQuality quality = defaults;
    quality.ssaa = ini.GetInt(section, "SSAA", quality.ssaa);
    quality.ambientStrength = ini.GetFloat(section, "AmbientStrength", quality.ambientStrength);
    quality.giSamples = ini.GetInt(section, "GISamples", quality.giSamples);
    quality.giBounces = ini.GetInt(section, "GIBounces", quality.giBounces);
    quality.giStrength = ini.GetFloat(section, "GIStrength", quality.giStrength);
    quality.reflStrength = ini.GetFloat(section, "ReflectionStrength", quality.reflStrength);
    quality.shininess = ini.GetFloat(section, "Shininess", quality.shininess);
    quality.shadowSamples = ini.GetInt(section, "ShadowSamples", quality.shadowSamples);
    quality.shadowSoftness = ini.GetFloat(section, "ShadowSoftness", quality.shadowSoftness);
    quality.exposureEV = ini.GetFloat(section, "ExposureEV", quality.exposureEV);
    quality.temporalFrames = ini.GetInt(section, "TemporalFrames", quality.temporalFrames);
    quality.anisotropy = ini.GetFloat(section, "Anisotropy", quality.anisotropy);
    return SanitizeRenderQuality(quality);
}

void WriteRenderQuality(
    std::ostream& output, const char* section, const FRenderQuality& quality)
{
    output << '[' << section << "]\n"
           << "SSAA = " << quality.ssaa << '\n'
           << "AmbientStrength = " << quality.ambientStrength << '\n'
           << "GISamples = " << quality.giSamples << '\n'
           << "GIBounces = " << quality.giBounces << '\n'
           << "GIStrength = " << quality.giStrength << '\n'
           << "ReflectionStrength = " << quality.reflStrength << '\n'
           << "Shininess = " << quality.shininess << '\n'
           << "ShadowSamples = " << quality.shadowSamples << '\n'
           << "ShadowSoftness = " << quality.shadowSoftness << '\n'
           << "ExposureEV = " << quality.exposureEV << '\n'
           << "TemporalFrames = " << quality.temporalFrames << '\n'
           << "Anisotropy = " << quality.anisotropy << '\n';
}
