#include "FFeatureLifecycle.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

void ValidateFeatureDescriptors(const std::vector<FFeatureDescriptor>& descriptors)
{
    std::unordered_set<std::string> ids;
    for (const FFeatureDescriptor& descriptor : descriptors)
    {
        if (descriptor.id.empty())
        {
            throw std::invalid_argument("Feature lifecycle descriptor has an empty ID");
        }
        if (!ids.insert(descriptor.id).second)
        {
            throw std::invalid_argument("Duplicate feature lifecycle ID: " + descriptor.id);
        }
        if (descriptor.purpose.empty())
        {
            throw std::invalid_argument("Feature lifecycle descriptor has no purpose: " + descriptor.id);
        }

        if (descriptor.lifecycle == EFeatureLifecycle::Deprecated)
        {
            if (descriptor.deprecatedVersion.empty() ||
                descriptor.replacement.empty() ||
                descriptor.warning.empty())
            {
                throw std::invalid_argument("Deprecated feature lifecycle descriptor is incomplete: " + descriptor.id);
            }
        }
        else if ((descriptor.lifecycle == EFeatureLifecycle::Stable ||
                  descriptor.lifecycle == EFeatureLifecycle::Experimental) &&
                 descriptor.introducedVersion.empty())
        {
            throw std::invalid_argument("Feature lifecycle descriptor has no introduced version: " + descriptor.id);
        }
    }
}

const std::vector<FFeatureDescriptor>& FFeatureLifecycleRegistry::All()
{
    static const std::vector<FFeatureDescriptor> descriptors = [] {
        std::vector<FFeatureDescriptor> values = {
            {
                "renderer.hardware_raster",
                "Hardware Rasterizer",
                EFeatureLifecycle::Stable,
                "2.0",
                "",
                "",
                EFeatureRemovalPolicy::None,
                "Provides normal primary visibility with hardware rasterization.",
                "",
            },
            {
                "renderer.ray_traced_effects",
                "Ray-traced Effects",
                EFeatureLifecycle::Stable,
                "2.0",
                "",
                "",
                EFeatureRemovalPolicy::None,
                "Layers optional secondary shadows, global illumination, and reflections over raster output.",
                "",
            },
            {
                "renderer.rt_compute_gl43",
                "OpenGL 4.3 Compute RT",
                EFeatureLifecycle::Experimental,
                "2.0",
                "",
                "Use renderer.ray_traced_effects with the Compatible path when OpenGL 4.3 Compute is unavailable.",
                EFeatureRemovalPolicy::None,
                "Accelerates ray-traced effects with OpenGL 4.3 Compute.",
                "",
            },
            {
                "renderer.cpu_software",
                "CPU Software Rasterizer",
                EFeatureLifecycle::Deprecated,
                "",
                "2.0",
                "renderer.hardware_raster",
                EFeatureRemovalPolicy::None,
                "Retained for education and raster regression comparison.",
                "Deprecated educational/regression renderer; use Hardware Rasterizer.",
            },
            {
                "renderer.gpu_pure_raytracer",
                "Pure GPU Ray Tracer",
                EFeatureLifecycle::Deprecated,
                "",
                "2.0",
                "Use renderer.hardware_raster with renderer.ray_traced_effects.",
                EFeatureRemovalPolicy::None,
                "Retained for whole-frame ray-tracing comparison and regression coverage.",
                "Deprecated comparison/regression renderer; use hardware raster plus ray-traced effects.",
            },
        };
        ValidateFeatureDescriptors(values);
        return values;
    }();
    return descriptors;
}

const FFeatureDescriptor& FFeatureLifecycleRegistry::Require(std::string_view id)
{
    const std::vector<FFeatureDescriptor>& descriptors = All();
    const auto found = std::find_if(
        descriptors.begin(), descriptors.end(),
        [id](const FFeatureDescriptor& descriptor) { return descriptor.id == id; });
    if (found == descriptors.end())
    {
        throw std::out_of_range("Unknown feature lifecycle ID: " + std::string(id));
    }
    return *found;
}
