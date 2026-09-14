#include "FGL43ComputeApi.h"

const std::array<const char*, 33>& FGL43ComputeApi::RequiredEntryPointNames()
{
    static const std::array<const char*, 33> names = {{
        "glDispatchCompute",
        "glActiveTexture",
        "glTexImage3D",
        "glBindBuffer",
        "glBufferData",
        "glDeleteBuffers",
        "glGenBuffers",
        "glAttachShader",
        "glCompileShader",
        "glCreateProgram",
        "glCreateShader",
        "glDeleteProgram",
        "glDeleteShader",
        "glGetProgramInfoLog",
        "glGetProgramiv",
        "glGetShaderInfoLog",
        "glGetShaderiv",
        "glGetUniformLocation",
        "glLinkProgram",
        "glShaderSource",
        "glUniform1f",
        "glUniform1i",
        "glUniform2i",
        "glUniform3fv",
        "glUseProgram",
        "glGetInteger64i_v",
        "glBindSampler",
        "glBindImageTexture",
        "glMemoryBarrier",
        "glGetInteger64v",
        "glBindBufferBase",
        "glBindBufferRange",
        "glGetIntegeri_v",
    }};
    return names;
}

bool FGL43ComputeApi::Load(const IGL43ProcAddressSource& source,
                           std::string* diagnostic)
{
    Reset();
    const auto& names = RequiredEntryPointNames();
    std::array<FGL43GenericProc, 33> resolved = {};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        resolved[i] = source.Resolve(names[i]);
        if (!resolved[i])
        {
            if (diagnostic)
                *diagnostic = std::string("OpenGL 4.3 Compute entry point is missing: ") +
                    names[i];
            Reset();
            return false;
        }
    }
    DispatchCompute = reinterpret_cast<FDispatchCompute>(resolved[0]);
    if (diagnostic) diagnostic->clear();
    return true;
}

bool FGL43ComputeApi::IsLoaded() const
{
    return DispatchCompute != nullptr;
}

void FGL43ComputeApi::Reset()
{
    DispatchCompute = nullptr;
}
