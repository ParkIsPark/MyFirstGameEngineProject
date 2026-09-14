#pragma once

#include <GL/glew.h>

#include <array>
#include <string>

using FGL43GenericProc = void (GLAPIENTRY*)();

class IGL43ProcAddressSource
{
public:
    virtual ~IGL43ProcAddressSource() = default;
    virtual FGL43GenericProc Resolve(const char* name) const = 0;
};

// Deliberately narrow OpenGL 4.3 surface. The repository keeps its historical
// GLEW headers; only entry points required by the optional Compute backend are
// loaded at runtime and validated before any call is made.
struct FGL43ComputeApi
{
#if defined(_WIN32)
    // The bundled GLEW header deliberately undefines APIENTRY/GLAPIENTRY at
    // EOF.  Spell out the Win32 OpenGL ABI here: a cdecl pointer corrupts the
    // 32-bit stack on the first dispatch even though address lookup succeeds.
    using FDispatchCompute = void (__stdcall*)(GLuint, GLuint, GLuint);
#else
    using FDispatchCompute = void (*)(GLuint, GLuint, GLuint);
#endif
    static const std::array<const char*, 1>& RequiredEntryPointNames();
    bool Load(const IGL43ProcAddressSource& source,
              std::string* diagnostic = nullptr);
    bool IsLoaded() const;
    void Reset();

    FDispatchCompute DispatchCompute = nullptr;
};
