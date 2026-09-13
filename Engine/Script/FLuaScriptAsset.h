#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

struct FLuaScriptAsset
{
    std::filesystem::path projectRelativePath;
    std::vector<std::byte> compiledChunk;
    // 64-bit FNV-1a over source bytes; an identity indicator, not a security hash.
    std::uint64_t contentHash = 0;
};
