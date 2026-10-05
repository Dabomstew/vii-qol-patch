#include "prepare_game.hpp"
#include "../preparer-common/preparer.hpp"
#include <windows.h>
#include <shellapi.h>
#include <set>
namespace fs = std::filesystem;
using namespace vii;
namespace {
std::wstring Wide(const std::string& s) { return preparer::Wide(s); }
std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, ' ');
    if (n) WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string Json(const std::string& value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+=' ';else out+=c;}return out+'"';}
void Print(const std::string& text){DWORD written=0;auto message=text+"\n";WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message.data(),DWORD(message.size()),&written,nullptr);}
preparer::View View(const fs::path& game, const prepare::Settings& settings) {
    preparer::View v; v.game = game; v.executable = game / L"NeptuniaVII.exe";
    v.gameName = L"Megadimension Neptunia VII";
    v.note = L"Your saved choices are loaded; advanced INI settings are kept. Loading improvements are on and gameplay options are off for a new installation.";
    v.snapshot = std::make_shared<prepare::Settings>(settings);
    v.paths = {{"cache", L"Cache folder...", settings.cache}, {"assets", L"Unpacked assets...", settings.assets}};
    for (size_t i = 0; i < prepare::Features.size(); ++i)
        v.features.push_back({Utf8(prepare::Features[i].key), prepare::Features[i].label, i < 3 ? 0u : 1u, settings.enabled[i]});
    return v;
}
class Adapter final : public preparer::Adapter {
public:
    preparer::Product Describe() const override {
        return {L"VII Prepare Game", L"VII Speedrun Patch",
            L"Choose your game folder and settings. Prepare assets for faster loading, or install the patch and play."};
    }
    std::vector<fs::path> Detect() override { auto p = prepare::DetectGame(); return p.empty() ? std::vector<fs::path>{} : std::vector<fs::path>{p}; }
    preparer::View Open(const fs::path& path) override {
        auto game = prepare::GameFolder(path); return View(game, prepare::ReadSettings(game));
    }
    std::vector<preparer::Capability> Actions(const preparer::View&) const override {
        using A = preparer::Action;
        return {{A::Install}, {A::Prepare}, {A::Play}, {A::Rollback}, {A::Uninstall}, {A::Recover}};
    }
    preparer::Outcome Execute(preparer::Action action, const preparer::View& view,
            const preparer::Report& report, const preparer::Cancel& cancel) override {
        auto settings = *std::static_pointer_cast<const prepare::Settings>(view.snapshot);
        for (const auto& p : view.paths) {
            if (p.value.empty() || !p.value.is_absolute()) throw std::runtime_error("Choose a full path for both the cache and unpacked assets folders");
            (p.id == "cache" ? settings.cache : settings.assets) = p.value;
        }
        for (size_t i = 0; i < prepare::Features.size(); ++i)
            for (const auto& f : view.features) if (f.id == Utf8(prepare::Features[i].key)) settings.enabled[i] = f.value;
        using A = preparer::Action;
        try {
            if (action == A::Prepare) {
                auto result = prepare::Run(view.game, settings, [&](const prepare::Progress& p) {
                    if (report) report({L"Preparing", p.current, L"files", p.files, p.totalFiles});
                }, cancel);
                return {preparer::State::Completed, L"Preparation complete",
                    std::to_wstring(result.generated) + L" textures built, " + std::to_wstring(result.reused) +
                    L" reused; " + std::to_wstring(result.unsupported) + L" unsupported sources will use normal game loading."};
            }
            std::wstring detail;
            if (action == A::Install) detail = prepare::ApplySettings(view.game, settings) ? L"Patch and settings applied." : L"Patch and settings are already up to date.";
            else if (action == A::Rollback) detail = prepare::RollbackGame(view.game);
            else if (action == A::Uninstall) { prepare::UninstallGame(view.game); detail = L"Patch uninstalled. Your settings and prepared data were kept."; }
            else if (action == A::Recover) { prepare::RecoverGame(view.game); detail = L"Interrupted update recovered. Installed files checked."; }
            else throw std::runtime_error("Unsupported action");
            return {preparer::State::Completed, L"Action complete", detail};
        } catch (const std::exception& e) {
            if (action == A::Prepare && std::string(e.what()).rfind("Cancelled", 0) == 0) throw preparer::Cancelled(e.what());
            throw;
        }
    }
};
int Gui(const fs::path& source, const prepare::Settings& settings) {
    Adapter adapter; auto view = View(source, settings);
    return preparer::RunGui(adapter, source.empty() ? nullptr : &view);
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);fs::path source,output,assets;bool gui=argc==1,install=false,recover=false,rollback=false,uninstall=false,query=false;
    std::vector<std::pair<size_t,bool>> changes;std::set<size_t> keys;std::string expectedHash;bool hasExpected=false;
    try{
        for(int i=1;i<argc;++i){std::wstring arg=argv[i];
            if(arg==L"--gui")gui=true;else if(arg==L"--enable-only"||arg==L"--install")install=true;else if(arg==L"--recover")recover=true;else if(arg==L"--rollback")rollback=true;else if(arg==L"--uninstall")uninstall=true;else if(arg==L"--settings")query=true;
            else if((arg==L"--source"||arg==L"--output"||arg==L"--assets")&&i+1<argc)(arg==L"--source"?source:arg==L"--output"?output:assets)=argv[++i];
            else if(arg==L"--if-config-hash"&&i+1<argc){expectedHash=Utf8(argv[++i]);hasExpected=true;}
            else if(arg==L"--set"&&i+1<argc){std::wstring value=argv[++i];auto split=value.find(L'=');size_t k=0;for(;k<prepare::Features.size();++k)if(value.substr(0,split)==prepare::Features[k].key)break;
                if(split==std::wstring::npos||k==prepare::Features.size()||(value.substr(split+1)!=L"0"&&value.substr(split+1)!=L"1")||!keys.insert(k).second)throw std::runtime_error("Use --set KnownFeature=0|1 once per feature");changes.emplace_back(k,value.substr(split+1)==L"1");
            }else if(arg==L"--help"){Print(
                "VII Prepare Game\n"
                "  --source GAME          Prepare or resume this game installation.\n"
                "  --install              Install the patch and save settings without preparing assets.\n"
                "                         --enable-only is an alias for --install.\n"
                "  --output CACHE         Choose the texture cache folder.\n"
                "  --assets FOLDER        Choose the unpacked assets folder.\n"
                "  --set Feature=0|1      Turn a feature off or on.\n"
                "  --settings             Print saved settings as JSON.\n"
                "  --if-config-hash HASH  Apply only if the settings still match this hash.\n"
                "                         An empty hash means no INI file existed.\n"
                "  --gui                  Open the window without starting work.\n"
                "  --rollback             Undo the last patch update.\n"
                "  --uninstall            Remove the patch; keep settings and prepared data.\n"
                "  --recover              Recover an interrupted update.\n"
                "Close the game before preparing, installing or using maintenance commands.");LocalFree(argv);return 0;}else throw std::runtime_error("Unknown or incomplete command-line option");
        }
        LocalFree(argv);argv=nullptr;const int actions=int(recover)+int(rollback)+int(uninstall)+int(install)+int(query);const bool overrides=!output.empty()||!assets.empty()||!changes.empty()||hasExpected;
        if(actions>1||(gui&&actions)||((recover||rollback||uninstall||query)&&overrides))throw std::runtime_error("Select one action; settings overrides apply only to install, prepare or the window");
        if(source.empty())source=prepare::DetectGame();if(!source.empty())source=prepare::GameFolder(source);if(source.empty()&&!gui)throw std::runtime_error("Game not detected; supply --source GAME");
        if(rollback){Print(Utf8(prepare::RollbackGame(source)));return 0;}if(uninstall){prepare::UninstallGame(source);Print("Patch uninstalled. Your settings and prepared data were kept.");return 0;}if(recover){prepare::RecoverGame(source);Print("Interrupted update recovered. Installed files checked.");return 0;}
        auto settings=source.empty()?prepare::Settings{}:prepare::ReadSettings(source);if(!output.empty())settings.cache=fs::absolute(output).lexically_normal();if(!assets.empty())settings.assets=fs::absolute(assets).lexically_normal();for(const auto& change:changes)settings.enabled[change.first]=change.second;if(hasExpected)settings.configHash=expectedHash;
        if(gui)return Gui(source,settings);
        if(query){std::string result="{\"cache\":"+Json(Utf8(settings.cache.wstring()))+",\"assets\":"+Json(Utf8(settings.assets.wstring()))+",\"config_sha256\":"+Json(settings.configHash)+",\"features\":{";for(size_t i=0;i<prepare::Features.size();++i){if(i)result+=',';result+=Json(Utf8(prepare::Features[i].key))+":"+(settings.enabled[i]?"true":"false");}Print(result+"}}");return 0;}
        if(install){Print(prepare::ApplySettings(source,settings)?"Patch and settings applied.":"Patch and settings are already up to date.");return 0;}
        uint64_t last=UINT64_MAX;auto report=[&](const prepare::Progress& p){if(p.files/1000!=last){last=p.files/1000;Print("{\"progress_files\":"+std::to_string(p.files)+",\"total_files\":"+std::to_string(p.totalFiles)+",\"bytes\":"+std::to_string(p.bytes)+"}");}};
        auto result=prepare::Run(source,settings,report);Print("{\"complete\":true,\"generated\":"+std::to_string(result.generated)+",\"reused\":"+std::to_string(result.reused)+",\"unsupported\":"+std::to_string(result.unsupported)+"}");return 0;
    }catch(const std::exception& e){if(argv)LocalFree(argv);Print("{\"error\":"+Json(e.what())+"}");if(gui)MessageBoxW(nullptr,Wide(e.what()).c_str(),L"VII Prepare Game",MB_OK|MB_ICONERROR);return 1;}
}
