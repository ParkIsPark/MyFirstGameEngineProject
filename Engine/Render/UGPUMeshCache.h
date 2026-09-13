#pragma once

#include "FGPUMeshResource.h"
#include "UMesh.h"

#include <cstdint>
#include <unordered_map>

class UGPUMeshCache
{
public:
    // The adapter is borrowed and must outlive the cache. In a real GL adapter,
    // callers must Clear/change generation while the owning context is valid.
    explicit UGPUMeshCache(IMeshGPUUploadAdapter& uploadAdapter);
    ~UGPUMeshCache() noexcept;

    UGPUMeshCache(const UGPUMeshCache&) = delete;
    UGPUMeshCache& operator=(const UGPUMeshCache&) = delete;

    const FGPUMeshResource& Acquire(const UMesh& mesh,
                                    std::uint64_t contextGeneration);

    void BeginFrame();
    void ReleaseUnused() noexcept;
    void Clear() noexcept;

    const FMeshGPUCacheStats& Stats() const { return stats_; }

private:
    struct FEntry
    {
        FGPUMeshResource resource;
        std::uint64_t lastUsedFrame = 0;
    };

    void Release(FEntry& entry) noexcept;

    IMeshGPUUploadAdapter& uploadAdapter_;
    std::unordered_map<FMeshAssetId, FEntry> entries_;
    FMeshGPUCacheStats stats_;
    std::uint64_t frame_ = 1;
    std::uint64_t contextGeneration_ = 0;
    bool hasContextGeneration_ = false;
};
