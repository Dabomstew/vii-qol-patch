#pragma once
#include "texture_prepare.hpp"
#include "prepare_settings.hpp"
namespace vii::prepare {
std::filesystem::path GameFolder(const std::filesystem::path& selected);
std::filesystem::path DetectGame();
std::filesystem::path CacheFolder(const std::filesystem::path& game);
Settings ReadSettings(const std::filesystem::path& game);
bool ApplySettings(const std::filesystem::path& game, const Settings& settings);
void EnableGame(const std::filesystem::path& game,const std::filesystem::path& cache);
void RecoverGame(const std::filesystem::path& game);
std::wstring RollbackGame(const std::filesystem::path& game);
void UninstallGame(const std::filesystem::path& game);
Result Run(const std::filesystem::path& game,const std::filesystem::path& cache,
    const Report& report={},const pac::Cancel& cancel={});
Result Run(const std::filesystem::path& game,const Settings& settings,
    const Report& report={},const pac::Cancel& cancel={});
}
