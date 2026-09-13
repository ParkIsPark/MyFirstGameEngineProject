#include "FLuaScriptCache.h"
#include "LuaInclude.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    std::filesystem::path CanonicalPath(const std::filesystem::path& path)
    {
#ifdef _WIN32
        // Some MinGW standard libraries only normalize junction paths. The OS
        // handle API follows all reparse points and returns the actual target.
        const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "cannot resolve script path");
        struct CloseHandleOnExit
        {
            HANDLE handle;
            ~CloseHandleOnExit() { CloseHandle(handle); }
        } close{handle};
        const DWORD length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!length) throw std::runtime_error("cannot resolve final script path");
        std::wstring resolved(length, L'\0');
        const DWORD written = GetFinalPathNameByHandleW(handle, resolved.data(), length, FILE_NAME_NORMALIZED);
        if (!written || written >= length) throw std::runtime_error("cannot resolve final script path");
        resolved.resize(written);
        // MinGW's filesystem status/ifstream do not consistently accept the
        // extended prefix. Convert both root and candidate to ordinary paths.
        if (resolved.compare(0, 8, L"\\\\?\\UNC\\") == 0) resolved.replace(0, 8, L"\\\\");
        else if (resolved.compare(0, 4, L"\\\\?\\") == 0) resolved.erase(0, 4);
        return std::filesystem::path(resolved).lexically_normal();
#else
        return std::filesystem::canonical(path);
#endif
    }

    bool SameComponent(const std::filesystem::path& left, const std::filesystem::path& right)
    {
#ifdef _WIN32
        return CompareStringOrdinal(left.c_str(), static_cast<int>(left.native().size()),
            right.c_str(), static_cast<int>(right.native().size()), TRUE) == CSTR_EQUAL;
#else
        return left == right;
#endif
    }

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
            if (part == child.end() || !SameComponent(*part, expected)) return false;
            ++part;
        }
        return part != child.end();
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
    if (!std::filesystem::is_directory(projectRoot_))
        throw std::invalid_argument("Lua project root must be a directory");
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

        const auto canonical = CanonicalPath(projectRoot_ / normalized);
        // Compare components, not textual prefixes. Resolve symlinks/junctions
        // first; even a link into another project directory is outside Scripts.
        if (!IsWithin(canonical, projectRoot_ / "Content/Scripts"))
            throw std::runtime_error("canonical path escapes Content/Scripts");
        if (!std::filesystem::is_regular_file(canonical))
            throw std::runtime_error("script is not a regular file");
        std::ifstream input(canonical, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open script source");
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad()) throw std::runtime_error("cannot read script source");

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
            const char* detail = lua_tostring(state_, -1);
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
