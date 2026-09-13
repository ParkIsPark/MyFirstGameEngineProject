#include "UGPUMeshCache.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace
{
FMeshGPUUploadView UploadView(const UMesh& mesh)
{
    FMeshGPUUploadView upload;
    upload.vertices = mesh.vertices.empty() ? nullptr : mesh.vertices.data();
    upload.vertexCount = mesh.vertices.size();
    upload.indices = mesh.indices.empty() ? nullptr : mesh.indices.data();
    upload.indexCount = mesh.indices.size();
    return upload;
}
} // namespace

UGPUMeshCache::UGPUMeshCache(IMeshGPUUploadAdapter& uploadAdapter)
    : uploadAdapter_(uploadAdapter)
{
}

UGPUMeshCache::~UGPUMeshCache() noexcept
{
    Clear();
}

const FGPUMeshResource& UGPUMeshCache::Acquire(
    const UMesh& mesh,
    std::uint64_t contextGeneration)
{
    if (contextGeneration == 0 ||
        uploadAdapter_.ActiveContextGeneration() != contextGeneration)
        throw std::invalid_argument(
            "GPU mesh cache context generation does not match the active context");

    if (hasContextGeneration_ && contextGeneration_ != contextGeneration)
        AbandonAll();

    contextGeneration_ = contextGeneration;
    hasContextGeneration_ = true;

    const FMeshAssetId assetId = mesh.AssetId();
    auto found = entries_.find(assetId);
    if (found == entries_.end())
    {
        FEntry entry;
        entry.resource = uploadAdapter_.CreateAndUpload(
            UploadView(mesh), contextGeneration);
        if (entry.resource.contextGeneration == 0 ||
            entry.resource.contextGeneration != contextGeneration)
        {
            uploadAdapter_.Abandon(entry.resource);
            throw std::runtime_error(
                "GPU mesh upload returned an invalid context generation label");
        }
        entry.resource.uploadedRevision = mesh.GeometryRevision();
        entry.resource.indexCount = mesh.indices.size();
        entry.lastUsedFrame = frame_;

        try
        {
            found = entries_.emplace(assetId, entry).first;
        }
        catch (...)
        {
            uploadAdapter_.Destroy(entry.resource);
            throw;
        }
        ++stats_.uploads;
        stats_.residentResources = entries_.size();
    }
    else
    {
        FEntry& entry = found->second;
        if (entry.resource.uploadedRevision != mesh.GeometryRevision())
        {
            std::string diagnostic;
            if (!uploadAdapter_.Reupload(entry.resource, UploadView(mesh), diagnostic))
            {
                ++stats_.failedReuploads;
                throw std::runtime_error(diagnostic.empty()
                    ? "GPU mesh reupload failed" : diagnostic);
            }
            entry.resource.uploadedRevision = mesh.GeometryRevision();
            entry.resource.indexCount = mesh.indices.size();
            ++stats_.reuploads;
        }
        entry.lastUsedFrame = frame_;
    }

    return found->second.resource;
}

void UGPUMeshCache::BeginFrame()
{
    if (frame_ == std::numeric_limits<std::uint64_t>::max())
    {
        Clear();
        frame_ = 1;
        return;
    }
    ++frame_;
}

void UGPUMeshCache::ReleaseUnused() noexcept
{
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->second.lastUsedFrame == frame_)
        {
            ++it;
            continue;
        }

        Release(it->second);
        it = entries_.erase(it);
    }
    stats_.residentResources = entries_.size();
}

void UGPUMeshCache::Clear() noexcept
{
    for (auto& pair : entries_)
        Release(pair.second);
    entries_.clear();
    stats_.residentResources = 0;
}

void UGPUMeshCache::AbandonAll() noexcept
{
    for (auto& pair : entries_)
    {
        uploadAdapter_.Abandon(pair.second.resource);
        ++stats_.abandons;
    }
    entries_.clear();
    stats_.residentResources = 0;
}

void UGPUMeshCache::Release(FEntry& entry) noexcept
{
    uploadAdapter_.Destroy(entry.resource);
    ++stats_.releases;
}
