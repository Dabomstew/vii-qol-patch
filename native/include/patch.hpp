#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace vii {
using Digest = std::array<unsigned char,32>;
struct Context { HMODULE game; std::wstring directory; std::wstring ini; };
using Installer = bool (*)(const Context&);
struct Patch { const wchar_t* name; Installer install; int defaultEnabled = 1; };
void Initialize(HMODULE proxy) noexcept;
void Log(const char* format, ...) noexcept;
Digest Hash(const void* data, size_t size);
Digest HashFile(const std::wstring& path);
std::wstring ModulePath(HMODULE module);
int Option(const Context&, const wchar_t* section, const wchar_t* key, int fallback);
bool ReplaceImport(const Context&, uintptr_t rva, void* expected, void* replacement);
bool InstallMipCache(const Context&);
bool InstallMotionCache(const Context&);
bool InstallAutoSkipEvents(const Context&);
bool InstallEventSkipBuffer(const Context&);
bool InstallSuppressTutorials(const Context&);
bool InstallSuppressDungeonPreview(const Context&);
bool InstallBattleAutoSkip(const Context&);
bool InstallJPBattleBalance(const Context&);
bool InstallLooseFiles(const Context&);
bool InstallNeptasm(const Context&);
bool InstallNewGameDetector(const Context&);
bool InstallLoadTiming(const Context&);
void ProbeStartup(const Context&) noexcept;
}
