#include "URasterLightingPass.h"

#include "FRenderQuality.h"
#include "FRenderScene.h"
#include "FRenderTarget.h"
#include "FPixelUnpackGuard.h"
#include "Shaders/RasterLightingShaders.h"
#include "Shaders/SharedLightingShaderSource.h"
#include "UHardwareGBuffer.h"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "stb_image.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
constexpr int TextureUnitCount = 10;
constexpr int DrawBufferCount = 8;
constexpr int ShaderPointLightCapacity = 16;
constexpr int OpenGLErrorDrainLimit = 16;

bool CompileStage(GLenum stage, const char* source, const char* label,
                  GLuint& shader, std::string& diagnostic)
{
    shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = std::string("Raster lighting ") + label + " shader compile failed: " + log;
    return false;
}

bool LinkProgram(const char* fragmentSource, const char* label,
                 GLuint& program, std::string& diagnostic)
{
    GLuint vertex = 0, fragment = 0;
    if (!CompileStage(GL_VERTEX_SHADER, RasterLightingShaders::FullscreenVertex,
                      "fullscreen vertex", vertex, diagnostic) ||
        !CompileStage(GL_FRAGMENT_SHADER, fragmentSource, label, fragment, diagnostic))
    {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return true;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    diagnostic = std::string("Raster lighting ") + label + " program link failed: " + log;
    glDeleteProgram(program);
    program = 0;
    return false;
}

struct FFullscreenState
{
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint viewport[4] = {};
    GLint program = 0;
    GLint vao = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint textures[TextureUnitCount] = {};
    GLint samplers[TextureUnitCount] = {};
    GLint drawBuffers[DrawBufferCount] = {};
    GLint depthFunction = GL_LESS;
    GLint frontFace = GL_CCW;
    GLint scissorBox[4] = {};
    GLint polygonMode[2] = {GL_FILL, GL_FILL};
    GLdouble depthRange[2] = {0.0, 1.0};
    GLboolean colorMasks[DrawBufferCount][4] = {};
    GLboolean blends[DrawBufferCount] = {};
    GLboolean depthMask = GL_TRUE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean framebufferSRGB = GL_FALSE;
    GLboolean rasterizerDiscard = GL_FALSE;
    GLboolean stencil = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLboolean sampleCoverage = GL_FALSE;
    GLboolean dither = GL_FALSE;
    GLboolean primitiveRestart = GL_FALSE;
    GLboolean depthClamp = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE;
};

FFullscreenState CaptureState()
{
    FFullscreenState state;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    for (int drawBuffer = 0; drawBuffer < DrawBufferCount; ++drawBuffer)
        glGetIntegerv(GL_DRAW_BUFFER0 + drawBuffer,
                      &state.drawBuffers[drawBuffer]);
    for (int unit = 0; unit < TextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[unit]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &state.samplers[unit]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode);
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange);
    for (int drawBuffer = 0; drawBuffer < DrawBufferCount; ++drawBuffer)
    {
        glGetBooleani_v(GL_COLOR_WRITEMASK, drawBuffer,
                        state.colorMasks[drawBuffer]);
        state.blends[drawBuffer] = glIsEnabledi(GL_BLEND, drawBuffer);
    }
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    state.framebufferSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    state.rasterizerDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
    state.stencil = glIsEnabled(GL_STENCIL_TEST);
    state.sampleAlphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    state.sampleCoverage = glIsEnabled(GL_SAMPLE_COVERAGE);
    state.dither = glIsEnabled(GL_DITHER);
    state.primitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART);
    state.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
    state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    return state;
}

void RestoreState(const FFullscreenState& state)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.readFramebuffer));
    if (state.drawFramebuffer == 0)
        glDrawBuffer(static_cast<GLenum>(state.drawBuffers[0]));
    else
    {
        GLenum drawBuffers[DrawBufferCount] = {};
        for (int drawBuffer = 0; drawBuffer < DrawBufferCount; ++drawBuffer)
            drawBuffers[drawBuffer] = static_cast<GLenum>(state.drawBuffers[drawBuffer]);
        glDrawBuffers(DrawBufferCount, drawBuffers);
    }
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glUseProgram(static_cast<GLuint>(state.program));
    glBindVertexArray(static_cast<GLuint>(state.vao));
    for (int unit = 0; unit < TextureUnitCount; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[unit]));
        glBindSampler(unit, static_cast<GLuint>(state.samplers[unit]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    if (state.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    for (int drawBuffer = 0; drawBuffer < DrawBufferCount; ++drawBuffer)
    {
        if (state.blends[drawBuffer]) glEnablei(GL_BLEND, drawBuffer);
        else glDisablei(GL_BLEND, drawBuffer);
        glColorMaski(drawBuffer,
                     state.colorMasks[drawBuffer][0],
                     state.colorMasks[drawBuffer][1],
                     state.colorMasks[drawBuffer][2],
                     state.colorMasks[drawBuffer][3]);
    }
    if (state.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (state.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (state.framebufferSRGB) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
    if (state.rasterizerDiscard) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD);
    if (state.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (state.sampleAlphaToCoverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (state.sampleCoverage) glEnable(GL_SAMPLE_COVERAGE); else glDisable(GL_SAMPLE_COVERAGE);
    if (state.dither) glEnable(GL_DITHER); else glDisable(GL_DITHER);
    if (state.primitiveRestart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART);
    if (state.depthClamp) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
    if (state.polygonOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthMask);
    glDepthRange(state.depthRange[0], state.depthRange[1]);
    glScissor(state.scissorBox[0], state.scissorBox[1],
              state.scissorBox[2], state.scissorBox[3]);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    glFrontFace(static_cast<GLenum>(state.frontFace));
}

class FStateGuard
{
public:
    FStateGuard() : state_(CaptureState()) {}
    ~FStateGuard() { RestoreState(state_); }
private:
    FFullscreenState state_;
};

void ConfigureFullscreenState()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    for (int drawBuffer = 0; drawBuffer < DrawBufferCount; ++drawBuffer)
    {
        glDisablei(GL_BLEND, drawBuffer);
        glColorMaski(drawBuffer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glDisable(GL_DITHER);
    glDisable(GL_PRIMITIVE_RESTART);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthRange(0.0, 1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

std::uint64_t EnvironmentStamp(const std::string& path)
{
    if (path.empty()) return 0;
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (error) return 1;
    return static_cast<std::uint64_t>(stamp.time_since_epoch().count());
}

std::uint64_t LightOverflowSignature(const FRenderScene& scene,
                                     std::size_t retained)
{
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](const void* bytes, std::size_t count)
    {
        const unsigned char* data = static_cast<const unsigned char*>(bytes);
        for (std::size_t i = 0; i < count; ++i)
        {
            hash ^= data[i];
            hash *= 1099511628211ull;
        }
    };
    const std::size_t count = scene.pointLights.size();
    mix(&count, sizeof(count));
    mix(&retained, sizeof(retained));
    // Positions/radiances may animate every frame. The overflow condition is
    // stable while the submitted and retained counts are unchanged.
    return hash;
}

bool CollectError(const char* operation, std::string* diagnostic)
{
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    if (diagnostic)
    {
        std::ostringstream stream;
        stream << operation << " produced OpenGL error 0x" << std::hex << error;
        *diagnostic = stream.str();
    }
    return false;
}

bool DrainPreexistingErrors(std::string* diagnostic)
{
    for (int attempt = 0; attempt < OpenGLErrorDrainLimit; ++attempt)
        if (glGetError() == GL_NO_ERROR) return true;
    if (diagnostic)
        *diagnostic = "HDRI upload could not drain the pre-existing OpenGL error queue";
    return false;
}

bool CollectUploadErrors(const char* operation, std::string* diagnostic)
{
    GLenum first = GL_NO_ERROR;
    bool drained = false;
    for (int attempt = 0; attempt < OpenGLErrorDrainLimit; ++attempt)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR)
        {
            drained = true;
            break;
        }
        if (first == GL_NO_ERROR) first = error;
    }
    if (first == GL_NO_ERROR) return true;
    if (diagnostic)
    {
        std::ostringstream stream;
        stream << operation << " produced OpenGL error 0x" << std::hex << first;
        if (!drained) stream << " (error queue did not drain within "
                             << std::dec << OpenGLErrorDrainLimit << " reads)";
        *diagnostic = stream.str();
    }
    return false;
}
} // namespace

URasterLightingPass::~URasterLightingPass() noexcept
{
    Shutdown();
}

bool URasterLightingPass::Init(std::uint64_t contextGeneration,
                               std::string* diagnostic)
{
    if (contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic) *diagnostic = "Raster lighting context generation does not match the active context";
        return false;
    }
    if (IsReady() && contextGeneration_ == contextGeneration) return true;
    if (contextGeneration_ != 0 && contextGeneration_ != contextGeneration)
        ForgetCurrentResources();

    FStateGuard restore;
    std::string error;
    const std::string lightingSource =
        SharedLightingShaderSource::BuildRasterLightingFragmentShader();
    if (!LinkProgram(lightingSource.c_str(), "lighting fragment",
                     lightingProgram_, error))
    {
        if (lightingProgram_) glDeleteProgram(lightingProgram_);
        lightingProgram_ = 0;
        if (diagnostic) *diagnostic = error;
        return false;
    }
    contextGeneration_ = contextGeneration;
    glGenVertexArrays(1, &fullscreenVAO_);
    if (!fullscreenVAO_)
    {
        if (diagnostic) *diagnostic = "Raster lighting failed to create fullscreen VAO";
        Shutdown();
        return false;
    }
    GLint fragmentUniformComponents = 0;
    GLint geometryUniformComponents = 0;
    GLint fragmentTextureUnits = 0;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &fragmentUniformComponents);
    glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, &geometryUniformComponents);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragmentTextureUnits);
    if (fragmentTextureUnits < TextureUnitCount || fragmentUniformComponents < 160 ||
        geometryUniformComponents < 160)
    {
        if (diagnostic) *diagnostic = "Raster lighting requires 10 fragment texture units and sufficient GL3.3 uniforms";
        Shutdown();
        return false;
    }
    pointLightLimit_ = std::min(ShaderPointLightCapacity,
        std::max(1, (std::min(fragmentUniformComponents,
                              geometryUniformComponents) - 64) / 6));
    stats_.pointLightLimit = static_cast<std::size_t>(pointLightLimit_);
    glUseProgram(lightingProgram_);
    const char* samplers[] = {"uPositionCoverage", "uGeometricNormal",
        "uShadingNormalModel", "uAlbedoShininess", "uSpecularMirror",
        "uIdentity", "uPrecomputedLighting", "uEmissive", "uDepth", "uSky"};
    for (int unit = 0; unit < TextureUnitCount; ++unit)
        glUniform1i(glGetUniformLocation(lightingProgram_, samplers[unit]), unit);
    if (diagnostic) diagnostic->clear();
    return CollectError("Raster lighting initialization", diagnostic);
}

bool URasterLightingPass::Resize(int width, int height,
                                 std::uint64_t contextGeneration,
                                 std::string* diagnostic)
{
    if (width <= 0 || height <= 0 || contextGeneration == 0 ||
        contextGeneration != ActiveRenderTargetContextGeneration())
    {
        if (diagnostic) *diagnostic = "Raster lighting rejected invalid output size/context";
        return false;
    }
    if (!Init(contextGeneration, diagnostic)) return false;
    if (framebuffer_ && width_ == width && height_ == height &&
        contextGeneration_ == contextGeneration)
        return true;

    FStateGuard restore;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (width > maxTextureSize || height > maxTextureSize)
    {
        if (diagnostic) *diagnostic = "Raster lighting output exceeds GL_MAX_TEXTURE_SIZE";
        return false;
    }
    FPixelUnpackGuard unpack;
    glActiveTexture(GL_TEXTURE0); // Captured by FStateGuard; never touch caller unit 10+.
    unsigned candidateFramebuffer = 0;
    unsigned candidateTextures[2] = {};
    glGenFramebuffers(1, &candidateFramebuffer);
    glGenTextures(2, candidateTextures);
    for (unsigned texture : candidateTextures)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, candidateFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, candidateTextures[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D, candidateTextures[1], 0);
    const GLenum candidateDrawBuffers[2] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, candidateDrawBuffers);
    const bool complete = candidateFramebuffer && candidateTextures[0] &&
        candidateTextures[1] &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete)
    {
        glDeleteTextures(2, candidateTextures);
        if (candidateFramebuffer) glDeleteFramebuffers(1, &candidateFramebuffer);
        if (diagnostic) *diagnostic = "Raster lighting HDR framebuffer is incomplete";
        return false;
    }
    if (!CollectError("Raster lighting resize", diagnostic))
    {
        glDeleteTextures(2, candidateTextures);
        glDeleteFramebuffers(1, &candidateFramebuffer);
        return false;
    }
    if (environmentAmbientTexture_) glDeleteTextures(1, &environmentAmbientTexture_);
    if (unshadowedDirectTexture_) glDeleteTextures(1, &unshadowedDirectTexture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = candidateFramebuffer;
    environmentAmbientTexture_ = candidateTextures[0];
    unshadowedDirectTexture_ = candidateTextures[1];
    width_ = width;
    height_ = height;
    ++resourceRevision_;
    ++stats_.outputAllocations;
    if (diagnostic) diagnostic->clear();
    return true;
}

bool URasterLightingPass::LoadEnvironmentTexture(
    const std::string& path, std::uint64_t contextGeneration,
    std::string* diagnostic)
{
    const std::uint64_t stamp = EnvironmentStamp(path);
    if (path == environmentPath_ && stamp == environmentStamp_)
        return true;
    if (path.empty())
    {
        if (environmentTexture_) glDeleteTextures(1, &environmentTexture_);
        environmentTexture_ = 0;
        environmentPath_.clear();
        environmentStamp_ = 0;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    stbi_set_flip_vertically_on_load(1);
    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, 3);
    if (!pixels)
    {
        const std::string warning = std::string("Raster lighting could not load HDRI '") +
            path + "': " + stbi_failure_reason();
        std::fprintf(stderr, "[Renderer] warning: %s; using the procedural sky\n",
                     warning.c_str());
        if (environmentTexture_) glDeleteTextures(1, &environmentTexture_);
        environmentTexture_ = 0;
        environmentPath_ = path;
        environmentStamp_ = stamp;
        if (diagnostic) diagnostic->clear();
        return true; // invalid environment image selects the procedural gradient
    }

    if (!DrainPreexistingErrors(diagnostic))
    {
        stbi_image_free(pixels);
        ++stats_.environmentTextureUploadFailures;
        return false;
    }
    FPixelUnpackGuard unpack;
    glActiveTexture(GL_TEXTURE0);
    unsigned candidate = 0;
    glGenTextures(1, &candidate);
    glBindTexture(GL_TEXTURE_2D, candidate);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0,
                 GL_RGB, GL_FLOAT, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);

    const bool injectedFailure = failNextEnvironmentTextureUploadForTesting_;
    failNextEnvironmentTextureUploadForTesting_ = false;
    if (injectedFailure || !candidate ||
        !CollectUploadErrors("HDRI texture upload", diagnostic))
    {
        if (candidate) glDeleteTextures(1, &candidate);
        if (injectedFailure && diagnostic)
            *diagnostic = "Injected HDRI texture upload failure";
        ++stats_.environmentTextureUploadFailures;
        return false;
    }

    const unsigned previous = environmentTexture_;
    environmentTexture_ = candidate;
    environmentPath_ = path;
    environmentStamp_ = stamp;
    if (previous) glDeleteTextures(1, &previous);
    ++stats_.environmentTextureUploads;
    (void)contextGeneration;
    if (diagnostic) diagnostic->clear();
    return true;
}

bool URasterLightingPass::PrepareEnvironment(
    const FRenderScene& scene, std::uint64_t contextGeneration,
    std::string* diagnostic)
{
    if (!Init(contextGeneration, diagnostic)) return false;
    FStateGuard restore;
    return LoadEnvironmentTexture(scene.environment.skyPath,
                                  contextGeneration, diagnostic);
}

bool URasterLightingPass::Render(const FRenderScene& scene,
                                 const FRenderQuality& quality,
                                 const UHardwareGBuffer& gbuffer,
                                 std::uint64_t contextGeneration,
                                 FRasterLightingOutput& output,
                                 std::string* diagnostic)
{
    output = {};
    if (!gbuffer.IsComplete() || gbuffer.ContextGeneration() != contextGeneration ||
        !Resize(gbuffer.Width(), gbuffer.Height(), contextGeneration, diagnostic))
        return false;

    if (!PrepareEnvironment(scene, contextGeneration, diagnostic)) return false;
    FStateGuard restore;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    const GLenum lightingDrawBuffers[2] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, lightingDrawBuffers);
    ConfigureFullscreenState();
    glUseProgram(lightingProgram_);
    glBindVertexArray(fullscreenVAO_);
    for (unsigned semantic = 0; semantic < 8; ++semantic)
    {
        glActiveTexture(GL_TEXTURE0 + semantic);
        glBindTexture(GL_TEXTURE_2D, gbuffer.Texture(
            static_cast<EHardwareGBufferSemantic>(semantic)));
        glBindSampler(semantic, 0);
    }
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, gbuffer.DepthTexture());
    glBindSampler(8, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, environmentTexture_);
    glBindSampler(9, 0);

    glUniform1i(glGetUniformLocation(lightingProgram_, "uHasSky"),
                environmentTexture_ ? 1 : 0);
    glUniform1i(glGetUniformLocation(lightingProgram_, "uDepthView"),
                quality.depthView ? 1 : 0);
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uEye"), 1,
                 glm::value_ptr(scene.camera.eye));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uCameraRight"), 1,
                 glm::value_ptr(scene.camera.right));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uCameraUp"), 1,
                 glm::value_ptr(scene.camera.up));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uCameraBackward"), 1,
                 glm::value_ptr(scene.camera.backward));
    glUniform4f(glGetUniformLocation(lightingProgram_, "uFrustum"),
                scene.camera.left, scene.camera.rightPlane,
                scene.camera.bottom, scene.camera.top);
    glUniform1f(glGetUniformLocation(lightingProgram_, "uNearDistance"),
                std::max(scene.camera.nearDistance, 0.0001f));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uEnvironmentTint"), 1,
                 glm::value_ptr(scene.environment.tint));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uSkyHorizon"), 1,
                 glm::value_ptr(scene.environment.horizon));
    glUniform3fv(glGetUniformLocation(lightingProgram_, "uSkyZenith"), 1,
                 glm::value_ptr(scene.environment.zenith));
    glUniform1f(glGetUniformLocation(lightingProgram_, "uSkyExponent"),
                scene.environment.exponent);
    glUniform1f(glGetUniformLocation(lightingProgram_, "uAmbientStrength"),
                quality.ambientStrength);

    const int lightCount = std::min<int>(pointLightLimit_,
        static_cast<int>(scene.pointLights.size()));
    stats_.uploadedPointLights = static_cast<std::size_t>(lightCount);
    stats_.truncatedPointLights = scene.pointLights.size() -
        static_cast<std::size_t>(lightCount);
    if (stats_.truncatedPointLights)
    {
        const std::uint64_t signature = LightOverflowSignature(
            scene, static_cast<std::size_t>(lightCount));
        if (overflowWarnings_.insert(signature).second)
        {
            std::fprintf(stderr,
                "[Renderer] warning: %zu point lights exceed the GL3.3 raster limit %d; truncating deterministically\n",
                scene.pointLights.size(), pointLightLimit_);
            ++stats_.overflowWarnings;
        }
    }
    glUniform1i(glGetUniformLocation(lightingProgram_, "uPointLightCount"), lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string positionName = "uPointLightPositions[" + std::to_string(i) + "]";
        const std::string sourceName = "uPointLightSources[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(lightingProgram_, positionName.c_str()), 1,
                     glm::value_ptr(scene.pointLights[static_cast<std::size_t>(i)].worldPosition));
        glUniform3fv(glGetUniformLocation(lightingProgram_, sourceName.c_str()), 1,
                     glm::value_ptr(scene.pointLights[static_cast<std::size_t>(i)].sourceIntensity));
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!CollectError("Raster lighting pass", diagnostic)) return false;
    ++stats_.lightingPasses;
    output.valid = true;
    output.environmentAmbientTarget = {
        static_cast<std::uint64_t>(environmentAmbientTexture_), width_, height_, true};
    output.unshadowedDirectTarget = {
        static_cast<std::uint64_t>(unshadowedDirectTexture_), width_, height_, true};
    output.emissiveTarget = {static_cast<std::uint64_t>(
        gbuffer.Texture(EHardwareGBufferSemantic::Emissive)), width_, height_, true};
    // A missing/invalid HDRI is a recoverable warning: the frame completed with
    // the procedural sky, so callers must not mistake the warning for failure.
    if (diagnostic) diagnostic->clear();
    return true;
}

void URasterLightingPass::Shutdown() noexcept
{
    if (contextGeneration_ != 0 &&
        contextGeneration_ == ActiveRenderTargetContextGeneration())
        DeleteCurrentResources();
    else
        ForgetCurrentResources();
}

void URasterLightingPass::DeleteCurrentResources() noexcept
{
    if (environmentTexture_) glDeleteTextures(1, &environmentTexture_);
    if (environmentAmbientTexture_) glDeleteTextures(1, &environmentAmbientTexture_);
    if (unshadowedDirectTexture_) glDeleteTextures(1, &unshadowedDirectTexture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    if (fullscreenVAO_) glDeleteVertexArrays(1, &fullscreenVAO_);
    if (lightingProgram_) glDeleteProgram(lightingProgram_);
    ForgetCurrentResources();
}

void URasterLightingPass::ForgetCurrentResources() noexcept
{
    lightingProgram_ = 0;
    fullscreenVAO_ = 0;
    framebuffer_ = 0;
    environmentAmbientTexture_ = 0;
    unshadowedDirectTexture_ = 0;
    environmentTexture_ = 0;
    width_ = height_ = 0;
    contextGeneration_ = 0;
    environmentPath_.clear();
    environmentStamp_ = 0;
    pointLightLimit_ = 0;
    overflowWarnings_.clear();
}
