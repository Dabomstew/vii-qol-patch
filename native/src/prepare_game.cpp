#include "prepare_game.hpp"
#include "prepare_install_internal.hpp"
#include <fstream>
#include <regex>
#pragma comment(lib,"advapi32.lib")
namespace vii::prepare {
namespace fs = std::filesystem;
using namespace install;
namespace {
bool Apply(Session& session, const Settings& settings) {
    const auto& game = session.game;
    ValidateSettings(game, settings);
    session.CheckUnchanged();
    SettingsFile staged(game);
    StageSettings(game, settings, staged.path);
    staged.Seal();
    const auto proxy = Resource(101);
    const auto wanted = Hex(Hash(proxy.data(), proxy.size()));
    const auto state = game / Names[2];
    const bool currentState = Ini(state,L"Install",L"Version") == L"2" &&
        Ini(state,L"Install",L"Active") == L"1" && Ini(state,L"Install",L"GameSHA256") == Wide(Baseline) &&
        Ini(state,L"Install",L"ProxySHA256",L"\x01") == L"\x01" &&
        Ini(state,L"Install",L"Cache") == CacheText(staged.path);
    session.CheckUnchanged();
    if (session.observed[0] == wanted && session.observed[1] == HashPath(staged.path) && currentState)
        return false; // Keep the last meaningful rollback snapshot intact.
    Transaction transaction(session, L"Install");
    Copy(staged.path, transaction.Stage(1));
    const auto tempState = transaction.Stage(2);
    NewIni(tempState);
    Set(tempState,L"Install",L"Version",L"2");
    Set(tempState,L"Install",L"Active",L"1");
    Set(tempState,L"Install",L"GameSHA256",Wide(Baseline));
    Set(tempState,L"Install",L"Backup",transaction.snapshot.directory.wstring());
    Set(tempState,L"Install",L"Cache",CacheText(staged.path));
    Flush(tempState);
    Write(transaction.Stage(0),proxy.data(),proxy.size());
    fs::create_directories(Native(settings.cache));
    transaction.Commit();
    return true;
}
}
fs::path GameFolder(const fs::path& selected){
    auto game=fs::absolute(selected).lexically_normal();if(game.filename().empty())game=game.parent_path();if(!_wcsicmp(game.filename().c_str(),L"CONTENTS"))game=game.parent_path();
    if(!fs::exists(Native(game/L"NeptuniaVII.exe"))||!fs::is_directory(Native(game/L"CONTENTS")))throw std::runtime_error("Select the installed Neptunia VII folder or its CONTENTS folder");
    SafeBelow(game,game);
    game=Normal(fs::canonical(Native(game)));
    Need(HashPath(SafeBelow(game,game/L"NeptuniaVII.exe")) == Baseline,
         "This game executable is not the supported Neptunia VII build");
    return game;
}
fs::path CacheFolder(const fs::path& game) { return ReadSettings(game).cache; }
fs::path DetectGame(){
    std::vector<fs::path> candidates;auto own=fs::path(ModulePath(GetModuleHandleW(nullptr))).parent_path();candidates.push_back(own);
    wchar_t path[32768]{};DWORD bytes=sizeof(path);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,path,&bytes)==ERROR_SUCCESS){
        fs::path steam(path);candidates.push_back(steam/L"steamapps/common/Megadimension Neptunia VII");
        std::ifstream file(steam/L"steamapps/libraryfolders.vdf");std::string line;std::regex pattern("\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"");std::smatch match;
        while(std::getline(file,line))if(std::regex_search(line,match,pattern)){
            auto value=match[1].str();for(size_t at=0;(at=value.find("\\\\",at))!=std::string::npos;)value.replace(at,2,"\\");
            candidates.push_back(fs::u8path(value)/L"steamapps/common/Megadimension Neptunia VII");}
    }
    for(const auto& candidate:candidates)try{return GameFolder(candidate);}catch(...){}return {};
}
bool ApplySettings(const fs::path& selected, const Settings& settings) {
    Session session(selected);
    return Apply(session, settings);
}
void EnableGame(const fs::path& selected,const fs::path& output) {
    auto settings = ReadSettings(selected);
    settings.cache = Normal(output);
    ApplySettings(selected, settings);
}
void RecoverGame(const fs::path& selected) {
    Session session(selected, true);
    Recover(session);
}
Result Run(const fs::path& selected,const fs::path& output,const Report& report,const pac::Cancel& cancel) {
    auto settings = ReadSettings(selected);
    settings.cache = Normal(output);
    return Run(selected, settings, report, cancel);
}
Result Run(const fs::path& selected,const Settings& settings,const Report& report,const pac::Cancel& cancel) {
    Session session(selected);
    const auto& game=session.game;
    ValidateSettings(game,settings);
    unpack::Run(game/L"CONTENTS",settings.assets,[&](const Progress& value){if(report){auto p=value;p.current=L"Extracting assets: "+p.current;report(p);}},cancel);
    if(cancel&&cancel())throw std::runtime_error("Cancelled; completed assets are preserved");
    const auto dlcAssets=unpack::FindDlc(game).empty()?fs::path{}:loose::DlcOutput(settings.assets);
    auto runtime=game/L"d3dx11_43.dll";
    auto result=Build(settings.assets,settings.cache,report,cancel,fs::exists(runtime)?runtime:fs::path{},dlcAssets);
    if(result.failed)throw std::runtime_error("Some textures could not be prepared. See the preparation report, resolve the failures, then resume");
    if(cancel&&cancel())throw std::runtime_error("Cancelled; completed textures are preserved");
    Apply(session,settings);
    return result;
}
}
