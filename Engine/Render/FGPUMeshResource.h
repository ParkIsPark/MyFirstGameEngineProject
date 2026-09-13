#pragma once

#include <cstddef>
#include <cstdint>

#include "Vertex.h"

struct FGPUMeshResource
{
    unsigned vao = 0;
    unsigned vertexBuffer = 0;
    unsigned indexBuffer = 0;
    std::uint64_t uploadedRevision = 0;
    std::size_t indexCount = 0;
};

struct FMeshGPUUploadView
{
    const Vertex* vertices = nullptr;
    std::size_t vertexCount = 0;
    const std::uint32_t* indices = nullptr;
    std::size_t indexCount = 0;
};

class IMeshGPUUploadAdapter
{
public:
    virtual ~IMeshGPUUploadAdapter() = default;

    virtual FGPUMeshResource CreateAndUpload(const FMeshGPUUploadView& upload) = 0;
    virtual void Reupload(FGPUMeshResource& resource,
                          const FMeshGPUUploadView& upload) noexcept = 0;
    virtual void Destroy(FGPUMeshResource& resource) noexcept = 0;
};

struct FMeshGPUCacheStats
{
    // Lifetime totals remain monotonic across Clear/context invalidation.
    // residentResources alone reflects the current cache contents.
    std::size_t residentResources = 0;
    std::uint64_t uploads = 0;
    std::uint64_t reuploads = 0;
    std::uint64_t releases = 0;
};
