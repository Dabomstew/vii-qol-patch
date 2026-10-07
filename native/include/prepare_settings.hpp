#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace vii::prepare {
struct Feature {
    const wchar_t* key;
    const wchar_t* label;
    bool prepareDefault;
    bool runtimeFallback;
};
// Runtime missing-key fallbacks for MotionCache/LooseFiles remain off.
// The preparer/template enable them explicitly; tests pin both contracts.
inline constexpr std::array<Feature, 10> Features = {{
    {L"MipCache", L"Reuse prepared textures", true, true},
    {L"MotionCache", L"Cache repeated motion data", true, false},
    {L"LooseFiles", L"Load unpacked assets", true, false},
    {L"UnlockFPSDuringLoads", L"Unlock FPS during loads", false, false},
    {L"AutoSkipEvents", L"Automatically skip story events", false, false},
    {L"EventSkipBuffer", L"Remember early skip input", false, false},
    {L"SuppressTutorials", L"Skip automatic tutorials", false, false},
    {L"BattleAutoSkip", L"Skip battle animations and results", false, false},
    {L"SuppressDungeonPreviews", L"Hide dungeon previews", false, false},
    {L"JPBattleBalance", L"Use JP battle balance", false, false},
}};
struct Settings {
    std::filesystem::path cache, assets;
    std::array<bool, Features.size()> enabled{};
    // Prevent applying stale controls over an INI changed while the window is open.
    std::string configHash;
};
}
