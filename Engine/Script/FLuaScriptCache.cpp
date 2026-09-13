#include "FLuaScriptCache.h"
#include "LuaInclude.h"

#include <array>
#include <cerrno>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    struct SourceHandle
    {
        HANDLE value;
        explicit SourceHandle(HANDLE handle) : value(handle)
        {
            if (value == INVALID_HANDLE_VALUE)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "cannot open script path");
        }
        ~SourceHandle() { CloseHandle(value); }
        SourceHandle(const SourceHandle&) = delete;
        SourceHandle& operator=(const SourceHandle&) = delete;
    };

    std::filesystem::path FinalPath(HANDLE handle)
    {
        // Some MinGW standard libraries only normalize junction paths. The OS
        // handle API follows all reparse points and returns the actual target.
        const DWORD length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!length) throw std::runtime_error("cannot resolve final script path");
        std::wstring resolved(length, L'\0');
        const DWORD written = GetFinalPathNameByHandleW(handle, resolved.data(), length, FILE_NAME_NORMALIZED);
        if (!written || written >= length) throw std::runtime_error("cannot resolve final script path");
        resolved.resize(written);
        // Normalize extended drive/UNC prefixes consistently for stored project
        // paths and comparisons. The open source handle is never replaced.
        if (resolved.compare(0, 8, L"\\\\?\\UNC\\") == 0) resolved.replace(0, 8, L"\\\\");
        else if (resolved.compare(0, 4, L"\\\\?\\") == 0) resolved.erase(0, 4);
        return std::filesystem::path(resolved).lexically_normal();
    }

    std::filesystem::path CanonicalPath(const std::filesystem::path& path)
    {
        const SourceHandle root(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(root.value, &information) ||
            !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::invalid_argument("Lua project root must be a directory");
        return FinalPath(root.value);
    }
#else
    struct SourceHandle
    {
        int value;
        explicit SourceHandle(int descriptor) : value(descriptor)
        {
            if (value < 0) throw std::system_error(errno, std::generic_category(), "cannot open script path");
        }
        ~SourceHandle() { close(value); }
        SourceHandle(const SourceHandle&) = delete;
        SourceHandle& operator=(const SourceHandle&) = delete;
    };

    std::filesystem::path CanonicalPath(const std::filesystem::path& path)
    {
        const auto canonical = std::filesystem::canonical(path);
        if (!std::filesystem::is_directory(canonical))
            throw std::invalid_argument("Lua project root must be a directory");
        return canonical;
    }
#endif

    struct StackRestore
    {
        lua_State* state;
        int top;
        ~StackRestore() { lua_settop(state, top); }
    };

    bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent)
    {
        auto part = child.begin();
        for (const auto& expected : parent)
        {
            // Conservative exact-case policy, including Windows: folding can
            // admit a distinct sibling in a case-sensitive NTFS directory.
            if (part == child.end() || *part != expected) return false;
            ++part;
        }
        return part != child.end();
    }

    std::string ReadValidatedSource(const std::filesystem::path& root,
        const std::filesystem::path& relative)
    {
#ifdef _WIN32
        // Allow renames, but keep reading this exact file object afterward.
        // Deny concurrent writers so a source read has stable bytes as well.
        const SourceHandle source(CreateFileW((root / relative).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        const auto finalPath = FinalPath(source.value);
        if (!IsWithin(finalPath, root / "Content/Scripts"))
            throw std::runtime_error("canonical path escapes Content/Scripts");
        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileType(source.value) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(source.value, &information) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("script is not a regular file");
#else
        // Descriptor-relative no-follow traversal anchors each step to the
        // already-open parent. Reject symlinks conservatively on POSIX; a
        // concurrent rename cannot redirect a later open through another path.
        SourceHandle source(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        for (auto part = relative.begin(); part != relative.end(); ++part)
        {
            const bool directory = std::next(part) != relative.end();
            const int next = openat(source.value, part->c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
                (directory ? O_DIRECTORY : O_NONBLOCK));
            if (next < 0) throw std::system_error(errno, std::generic_category(), "cannot open contained script path");
            close(source.value);
            source.value = next;
        }
        struct stat information{};
        if (fstat(source.value, &information) != 0 || !S_ISREG(information.st_mode))
            throw std::runtime_error("script is not a regular file");
#endif
        std::string bytes;
        std::array<char, 8192> buffer{};
        for (;;)
        {
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(source.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "cannot read script source");
#else
            const auto count = read(source.value, buffer.data(), buffer.size());
            if (count < 0)
            {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "cannot read script source");
            }
#endif
            if (!count) return bytes;
            bytes.append(buffer.data(), static_cast<size_t>(count));
        }
    }

    int WriteBytecode(lua_State*, const void* data, size_t size, void* target) noexcept
    {
        try
        {
            auto& bytes = *static_cast<std::vector<std::byte>*>(target);
            const auto* start = static_cast<const std::byte*>(data);
            if (size) bytes.insert(bytes.end(), start, start + size);
            return 0;
        }
        catch (...) { return 1; } // Never unwind a native exception through lua_dump.
    }
}

FLuaScriptCache::FLuaScriptCache(lua_State* state, std::filesystem::path projectRoot)
    : state_(state), projectRoot_(CanonicalPath(projectRoot))
{
    if (!state_) throw std::invalid_argument("Lua cache requires a live VM");
}

std::shared_ptr<const FLuaScriptAsset> FLuaScriptCache::Load(const std::filesystem::path& path)
{
    const std::string requested = path.generic_string();
    try
    {
        // Reject traversal before normalizing; Windows root names, root-relative
        // paths, drive-relative paths and alternate data streams are never assets.
        if (path.empty() || path.has_root_path() || requested.find(':') != std::string::npos ||
            requested.find('\0') != std::string::npos)
            throw std::runtime_error("requires a project-relative Content/Scripts path");
        for (const auto& part : path)
            if (part == "..") throw std::runtime_error("parent traversal is forbidden");
        const auto normalized = path.lexically_normal();
        if (!IsWithin(normalized, std::filesystem::path("Content/Scripts")))
            throw std::runtime_error("requires the Content/Scripts/ prefix");
        const auto cached = assets_.find(normalized);
        if (cached != assets_.end()) return cached->second;

        const std::string source = ReadValidatedSource(projectRoot_, normalized);

        auto asset = std::make_shared<FLuaScriptAsset>();
        asset->projectRelativePath = normalized;
        asset->contentHash = 14695981039346656037ull;
        for (const unsigned char byte : source)
        {
            asset->contentHash ^= byte;
            asset->contentHash *= 1099511628211ull;
        }
        const StackRestore restore{state_, lua_gettop(state_)};
        const std::string chunkName = "@" + normalized.generic_string();
        if (luaL_loadbufferx(state_, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK)
        {
            const char* detail = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
            throw std::runtime_error(std::string("compile: ") + (detail ? detail : "non-string Lua error"));
        }
        if (lua_dump(state_, WriteBytecode, &asset->compiledChunk, 0) != 0)
            throw std::runtime_error("compile: cannot allocate compiled bytecode");
        assets_.emplace(normalized, asset);
        return asset;
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error("Lua load [" + requested + "]: " + error.what());
    }
}

void FLuaScriptCache::Clear() { assets_.clear(); }
