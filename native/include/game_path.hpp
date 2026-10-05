#pragma once
#include <filesystem>
#include <string>

namespace vii {
inline std::filesystem::path ResolveGamePath(const std::wstring& game, const std::wstring& configured, const wchar_t* fallback) {
    std::filesystem::path value = configured.empty() ? std::filesystem::path(fallback) : std::filesystem::path(configured);
    return value.is_absolute() ? value : std::filesystem::path(game) / value;
}
}
