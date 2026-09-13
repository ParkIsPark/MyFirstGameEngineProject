#pragma once

#include <string>
#include <string_view>
#include <vector>

enum class EFeatureLifecycle
{
    Stable,
    Experimental,
    Deprecated,
    Internal,
};

enum class EFeatureRemovalPolicy
{
    None,
    Planned,
};

struct FFeatureDescriptor
{
    std::string id;
    std::string displayName;
    EFeatureLifecycle lifecycle;
    std::string introducedVersion;
    std::string deprecatedVersion;
    std::string replacement;
    EFeatureRemovalPolicy removalPolicy;
    std::string purpose;
    std::string warning;
};

void ValidateFeatureDescriptors(const std::vector<FFeatureDescriptor>& descriptors);

class FFeatureLifecycleRegistry
{
public:
    static const FFeatureDescriptor& Require(std::string_view id);
    static const std::vector<FFeatureDescriptor>& All();
};

#define ENGINE_DEPRECATED(message) [[deprecated(message)]]
