#include "prepare_install_internal.hpp"
#include "game_path.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace vii::prepare {
using namespace install;
namespace {
constexpr auto CacheDefault = L"vii-speedrun-patch\\cache";
constexpr auto AssetsDefault = L"vii-speedrun-patch\\unpacked";
struct Default { std::wstring section, key, value; };
std::string Trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r");
    return start == std::string::npos ? "" : text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}
std::vector<Default> Defaults() {
    auto bytes = Resource(103);
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::vector<Default> result;
    std::set<std::pair<std::wstring, std::wstring>> seen;
    std::string line;
    std::wstring section;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = Wide(line.substr(1, line.size() - 2)); continue; }
        auto split = line.find('=');
        Need(!section.empty() && split != std::string::npos, "Invalid bundled settings template");
        auto key = Wide(Trim(line.substr(0, split))), value = Wide(Trim(line.substr(split + 1)));
        Need(!key.empty() && seen.emplace(section, key).second, "Duplicate or empty template setting");
        result.push_back({section, key, value});
    }
    Need(!result.empty(), "Bundled settings template is empty");
    for (const auto& feature : Features) {
        auto found = std::find_if(result.begin(), result.end(), [&](const Default& d) {
            return d.section == L"Patches" && d.key == feature.key;
        });
        Need(found != result.end() && found->value == (feature.prepareDefault ? L"1" : L"0"),
             "Bundled feature defaults disagree with the preparer");
    }
    return result;
}
bool Has(const fs::path& file, const wchar_t* section, const wchar_t* key) {
    return Ini(file, section, key, L"\x01") != L"\x01";
}
bool Enabled(const fs::path& file, const Feature& feature) {
    return GetPrivateProfileIntW(L"Patches", feature.key, feature.prepareDefault ? 1 : 0, Native(file).c_str()) != 0;
}
bool SameLocation(const fs::path& a, const fs::path& b) {
    return Same(Normal(fs::weakly_canonical(Native(a))), Normal(fs::weakly_canonical(Native(b))));
}
bool Contains(const fs::path& root, const fs::path& child) {
    auto r = Normal(fs::weakly_canonical(Native(root))).wstring();
    auto c = Normal(fs::weakly_canonical(Native(child))).wstring();
    if (Same(r, c)) return true;
    if (r.back() != L'\\') r += L'\\';
    return c.size() > r.size() && CompareStringOrdinal(r.c_str(), int(r.size()), c.c_str(), int(r.size()), TRUE) == CSTR_EQUAL;
}
std::wstring PathText(const fs::path& game, const fs::path& requested, const std::wstring& prior,
                      const wchar_t* fallback, bool hadKey) {
    auto effective = ResolveGamePath(game.wstring(), prior, fallback);
    if (hadKey && SameLocation(effective, requested)) return prior;
    auto relative = Normal(requested).lexically_relative(game);
    return relative.empty() || relative.native().rfind(L"..", 0) == 0 ? Normal(requested).wstring() : relative.wstring();
}
void EnsureUnicode(const fs::path& file, const std::wstring& paths) {
    if (std::all_of(paths.begin(), paths.end(), [](wchar_t c) { return c < 128; })) return;
    std::ifstream input(Native(file), std::ios::binary);
    Need(bool(input), "Cannot read settings for Unicode conversion");
    std::string raw((std::istreambuf_iterator<char>(input)), {});
    input.close();
    if (raw.size() >= 2 && uint8_t(raw[0]) == 0xff && uint8_t(raw[1]) == 0xfe) return;
    // Preserve existing Windows ANSI profiles; a UTF-8 BOM selects UTF-8 explicitly.
    const bool utf8 = raw.size() >= 3 && uint8_t(raw[0]) == 0xef && uint8_t(raw[1]) == 0xbb && uint8_t(raw[2]) == 0xbf;
    if (utf8) raw.erase(0, 3);
    auto page = utf8 ? CP_UTF8 : CP_ACP;
    int n = MultiByteToWideChar(page, 0, raw.data(), int(raw.size()), nullptr, 0);
    Need(n > 0 || raw.empty(), "Cannot decode existing settings");
    std::wstring text(size_t(n), L' ');
    if (n) Need(MultiByteToWideChar(page, 0, raw.data(), int(raw.size()), text.data(), n) == n, "Cannot decode existing settings");
    std::ofstream output(Native(file), std::ios::binary | std::ios::trunc);
    const unsigned char bom[] = {0xff, 0xfe};
    output.write(reinterpret_cast<const char*>(bom), 2);
    output.write(reinterpret_cast<const char*>(text.data()), text.size() * 2);
    Need(bool(output), "Cannot preserve Unicode settings");
}
}
Settings ReadSettings(const fs::path& selected) {
    const auto game = GameFolder(selected), config = SafeBelow(game, game / L"vii-patches.ini");
    Settings result;
    result.configHash = HashPath(config);
    if (!result.configHash.empty()) {
        std::ifstream input(Native(config), std::ios::binary);
        unsigned char prefix[3]{};
        input.read(reinterpret_cast<char*>(prefix), 3);
        Need(!(prefix[0] == 0xef && prefix[1] == 0xbb && prefix[2] == 0xbf) &&
             !(prefix[0] == 0xfe && prefix[1] == 0xff),
             "Unsupported INI encoding. Save vii-patches.ini as Windows ANSI or UTF-16 LE, then reload settings");
    }
    result.cache = Normal(ResolveGamePath(game.wstring(), Ini(config, L"MipCache", L"DiskDirectory", CacheDefault), CacheDefault));
    result.assets = Normal(ResolveGamePath(game.wstring(), Ini(config, L"LooseFiles", L"Directory", AssetsDefault), AssetsDefault));
    for (size_t i = 0; i < Features.size(); ++i) result.enabled[i] = Enabled(config, Features[i]);
    Need(HashPath(config) == result.configHash, "Settings changed while being read; reload the game folder");
    return result;
}
namespace install {
SettingsFile::SettingsFile(const fs::path& game) : path(SafeBelow(game, game / (L".vii-settings-" + Stamp() + L".ini"))) {}
void SettingsFile::Seal() { ownedHash_ = HashPath(path); }
SettingsFile::~SettingsFile() {
    try { if (!ownedHash_.empty() && HashPath(path) == ownedHash_) DeleteFileW(Native(path).c_str()); }
    catch (...) { /* Retain unexpected files for inspection. */ }
}
std::wstring CacheText(const fs::path& config) { return Ini(config, L"MipCache", L"DiskDirectory", CacheDefault); }
void ValidateSettings(const fs::path& game, const Settings& settings) {
    Need(!settings.cache.empty() && !settings.assets.empty(), "Select both cache and unpacked asset folders");
    CheckOutput(game, settings.cache); CheckOutput(game, settings.assets);
    const auto dlc = loose::DlcOutput(settings.assets);
    CheckOutput(game, dlc);
    for (const auto& assets : {settings.assets, dlc})
        Need(!Contains(settings.cache, assets) && !Contains(assets, settings.cache),
             "Texture cache and unpacked assets must use separate, non-overlapping folders");
    Need(HashPath(SafeBelow(game, game / Names[1])) == settings.configHash,
         "Settings changed since they were loaded. Reload the game folder before applying choices");
}
void StageSettings(const fs::path& game, const Settings& settings, const fs::path& staged) {
    const auto config = game / Names[1];
    auto defaults = Defaults();
    const auto cacheText = PathText(game, settings.cache, Ini(config, L"MipCache", L"DiskDirectory"), CacheDefault,
                                    Has(config, L"MipCache", L"DiskDirectory"));
    const auto assetText = PathText(game, settings.assets, Ini(config, L"LooseFiles", L"Directory"), AssetsDefault,
                                    Has(config, L"LooseFiles", L"Directory"));
    if (!settings.configHash.empty()) Copy(config, staged);
    else { const auto bytes = Resource(103); Write(staged, bytes.data(), bytes.size()); }
    EnsureUnicode(staged, cacheText + assetText + game.wstring());
    for (const auto& d : defaults)
        if (!Has(staged, d.section.c_str(), d.key.c_str())) Set(staged, d.section.c_str(), d.key.c_str(), d.value);
    for (size_t i = 0; i < Features.size(); ++i)
        if (Enabled(staged, Features[i]) != settings.enabled[i])
            Set(staged, L"Patches", Features[i].key, settings.enabled[i] ? L"1" : L"0");
    if (Ini(staged, L"MipCache", L"DiskDirectory") != cacheText) Set(staged, L"MipCache", L"DiskDirectory", cacheText);
    if (Ini(staged, L"LooseFiles", L"Directory") != assetText) Set(staged, L"LooseFiles", L"Directory", assetText);
    Flush(staged);
}
}
}
