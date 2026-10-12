#pragma once
#include <windows.h>

namespace vii {
// Loader-safe import bootstrap only. The callback runs on the game's first D3D
// creation call, after loader initialization; it must retain baseline gating.
bool InstallEarlyGraphicsBootstrap(HMODULE game, void (*initialize)() noexcept) noexcept;
}
