#include "FGL43ComputeApi.h"

const std::array<const char*, 1>& FGL43ComputeApi::RequiredEntryPointNames()
{
    static const std::array<const char*, 1> names = {{
        "glDispatchCompute",
    }};
    return names;
}

bool FGL43ComputeApi::Load(const IGL43ProcAddressSource& source,
                           std::string* diagnostic)
{
    Reset();
    const auto& names = RequiredEntryPointNames();
    std::array<FGL43GenericProc, 1> resolved = {};
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
