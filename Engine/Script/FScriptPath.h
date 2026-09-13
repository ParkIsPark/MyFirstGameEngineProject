#pragma once

#include <filesystem>

// Lexical configuration validation shared by serialized script attachments.
// Filesystem containment and reparse-point protection remain FLuaScriptCache's
// responsibility when a script is actually opened.
std::filesystem::path NormalizeScriptProjectPath(const std::filesystem::path& path);
