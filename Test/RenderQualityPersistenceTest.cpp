// Current complete recipe:
// powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderQualityPersistenceTest
#include "FIniFile.h"
#include "FRenderQuality.h"
#include "FRenderQualitySettings.h"

#include <cassert>
#include <cmath>
#include <sstream>
#include <string>

namespace
{
bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

void CheckEveryField(const FRenderQuality& got, const FRenderQuality& expected)
{
    assert(got.ssaa == expected.ssaa);
    assert(Near(got.ambientStrength, expected.ambientStrength));
    assert(got.giSamples == expected.giSamples);
    assert(got.giBounces == expected.giBounces);
    assert(Near(got.giStrength, expected.giStrength));
    assert(Near(got.reflStrength, expected.reflStrength));
    assert(Near(got.shininess, expected.shininess));
    assert(got.shadowSamples == expected.shadowSamples);
    assert(Near(got.shadowSoftness, expected.shadowSoftness));
    assert(Near(got.exposureEV, expected.exposureEV));
    assert(got.temporalFrames == expected.temporalFrames);
    assert(Near(got.anisotropy, expected.anisotropy));
}
}

int main()
{
    // Mutation caught: omitting or renaming any persisted non-editor quality field.
    FRenderQuality authored;
    authored.ssaa = 2;
    authored.ambientStrength = 0.25f;
    authored.giSamples = 7;
    authored.giBounces = 3;
    authored.giStrength = 1.75f;
    authored.reflStrength = 0.625f;
    authored.shininess = 96.0f;
    authored.shadowSamples = 11;
    authored.shadowSoftness = 0.125f;
    authored.exposureEV = -1.5f;
    authored.temporalFrames = 19;
    authored.anisotropy = 12.0f;
    authored.depthView = true;

    std::ostringstream saved;
    WriteRenderQuality(saved, "Editor", authored);
    WriteRenderQuality(saved, "Game", authored);
    FIniFile roundTrip;
    roundTrip.Parse(saved.str());
    CheckEveryField(ReadRenderQuality(roundTrip, "Editor", {}), authored);
    CheckEveryField(ReadRenderQuality(roundTrip, "Game", {}), authored);
    assert(!roundTrip.Has("Editor", "DepthView"));

    // Mutation caught: new keys defaulting to zero or inheriting stale profile state.
    FIniFile old;
    old.Parse("[Editor]\nSSAA=2\nGISamples=8\n[Game]\nSSAA=1\nGISamples=2\n");
    const FRenderQuality correctedDefaults;
    const FRenderQuality oldEditor = ReadRenderQuality(old, "Editor", correctedDefaults);
    const FRenderQuality oldGame = ReadRenderQuality(old, "Game", correctedDefaults);
    assert(Near(oldEditor.exposureEV, 0.0f) && oldEditor.temporalFrames == 32 && Near(oldEditor.anisotropy, 8.0f));
    assert(Near(oldGame.exposureEV, 0.0f) && oldGame.temporalFrames == 32 && Near(oldGame.anisotropy, 8.0f));

    // Mutation caught: Editor and Game taking different clamping paths.
    FIniFile invalid;
    invalid.Parse(
        "[Editor]\nSSAA=0\nShadowSamples=-4\nGISamples=99\nGIBounces=-2\n"
        "ReflectionStrength=4\nExposureEV=-99\nTemporalFrames=0\nAnisotropy=99\n"
        "[Game]\nSSAA=-7\nShadowSamples=0\nGISamples=1000\nGIBounces=-8\n"
        "ReflectionStrength=8\nExposureEV=-1000\nTemporalFrames=-1\nAnisotropy=1000\n");
    const FRenderQuality editor = ReadRenderQuality(invalid, "Editor", correctedDefaults);
    const FRenderQuality game = ReadRenderQuality(invalid, "Game", correctedDefaults);
    CheckEveryField(editor, game);
    assert(editor.ssaa == 1 && editor.shadowSamples == 1 && editor.giSamples == 32);
    assert(editor.giBounces == 0 && Near(editor.reflStrength, 1.0f));
    assert(Near(editor.exposureEV, -16.0f) && editor.temporalFrames == 1 && Near(editor.anisotropy, 16.0f));
    return 0;
}
