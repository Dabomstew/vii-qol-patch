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
inline constexpr std::array<Feature, 11> Features = {{
    {L"MipCache", L"Reuse prepared textures", true, true},
    {L"MotionCache", L"Cache repeated motion data", true, false},
    {L"LooseFiles", L"Load unpacked assets", true, false},
    {L"UnlockFPSDuringLoads", L"Unlock FPS during loads", false, false},
    {L"SkipBattleLoadingPresent", L"Reduce battle loading frames", true, true},
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
    bool renderEnabled = false, renderChanged = false;
    int renderWidth = 0, renderHeight = 0;
};
struct RenderPreset { const wchar_t* label; int width, height; };
inline constexpr std::array<RenderPreset, 9> RenderPresets = {{
    {L"Disabled", 0, 0}, {L"640 x 360", 640, 360}, {L"854 x 480", 854, 480},
    {L"960 x 540", 960, 540}, {L"1280 x 720", 1280, 720},
    {L"1600 x 900", 1600, 900}, {L"1920 x 1080", 1920, 1080},
    {L"2560 x 1440", 2560, 1440}, {L"3840 x 2160", 3840, 2160},
}};
inline size_t RenderSelection(const Settings& s) {
    if (!s.renderEnabled) return 0;
    for (size_t i = 1; i < RenderPresets.size(); ++i)
        if (s.renderWidth == RenderPresets[i].width && s.renderHeight == RenderPresets[i].height) return i;
    return RenderPresets.size(); // Keep existing custom dimensions or scale.
}
inline void SelectRenderPreset(Settings& s, size_t selected) {
    const auto& preset = RenderPresets.at(selected);
    s.renderWidth = preset.width; s.renderHeight = preset.height;
    s.renderEnabled = selected != 0; s.renderChanged = true;
}
}
