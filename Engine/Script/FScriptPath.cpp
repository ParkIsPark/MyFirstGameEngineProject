#include "FScriptPath.h"

#include <algorithm>
#include <stdexcept>
#include <string>

std::filesystem::path NormalizeScriptProjectPath(const std::filesystem::path& path)
{
    std::string spelling = path.generic_string();
    std::replace(spelling.begin(), spelling.end(), '\\', '/');
    if (spelling.empty() || path.has_root_path() || spelling.find(':') != std::string::npos ||
        spelling.find('\0') != std::string::npos)
        throw std::invalid_argument("Script path must be a project-relative Content/Scripts file");
    if (spelling.back() == '/' || spelling == "." ||
        (spelling.size() >= 2 && spelling.compare(spelling.size() - 2, 2, "/.") == 0))
        throw std::invalid_argument("Script path must name a file, not a directory");

    for (const auto& segment : std::filesystem::path(spelling))
        if (segment == "..") throw std::invalid_argument("Script path traversal is forbidden");

    const std::filesystem::path normalized = std::filesystem::path(spelling).lexically_normal();
    auto part = normalized.begin();
    if (part == normalized.end() || *part++ != "Content" ||
        part == normalized.end() || *part++ != "Scripts" ||
        part == normalized.end())
        throw std::invalid_argument("Script path must name a file beneath Content/Scripts");
    return normalized;
}
