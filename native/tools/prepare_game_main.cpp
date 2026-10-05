#include "prepare_game.hpp"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"uuid.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace fs=std::filesystem;
using namespace vii;
namespace {
constexpr UINT Finished=WM_APP+1;
enum {SourceButton=101,OutputButton,StartButton,CancelButton,PlayButton,RollbackButton,UninstallButton,RecoverButton,AssetsButton,InstallButton,ReloadButton,FeatureFirst=200};
std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,' ');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::string Json(const std::string& value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+=' ';else out+=c;}return out+'"';}
void Print(const std::string& text){DWORD written=0;auto message=text+"\n";WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message.data(),DWORD(message.size()),&written,nullptr);}
bool Pick(HWND owner,const wchar_t* title,fs::path& result){
    IFileDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return false;
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);dialog->SetTitle(title);bool picked=false;
    if(SUCCEEDED(dialog->Show(owner))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){result=path;CoTaskMemFree(path);picked=true;}item->Release();}}
    dialog->Release();return picked;
}
struct App {
    HWND window=nullptr,sourceText=nullptr,outputText=nullptr,assetsText=nullptr,status=nullptr,detail=nullptr,bar=nullptr;
    HFONT font=nullptr,heading=nullptr;int dpi=96;fs::path source;prepare::Settings settings;
    bool busy=false,closeAfter=false;std::atomic<bool> cancel{false};uint64_t started=0;
    std::mutex mutex;prepare::Progress progress;prepare::Result result;std::wstring error,message;std::thread worker;int action=StartButton;
    ~App(){if(worker.joinable())worker.join();if(font)DeleteObject(font);if(heading)DeleteObject(heading);}
    int Px(int n)const{return MulDiv(n,dpi,96);}
    void Detail(std::wstring text){for(size_t i=0;(i=text.find(L'\n',i))!=std::wstring::npos;++i)if(i==0||text[i-1]!=L'\r'){text.insert(i,L"\r");++i;}SetWindowTextW(detail,text.c_str());}
    void Error(const std::exception& e){SetWindowTextW(status,L"Action stopped; see details below");Detail(Wide(e.what()));}
    void Enable(bool enabled){
        for(int id:{SourceButton,OutputButton,AssetsButton,StartButton,PlayButton,InstallButton,ReloadButton,RollbackButton,UninstallButton,RecoverButton})EnableWindow(GetDlgItem(window,id),enabled&&(id==SourceButton||!source.empty()));
        for(size_t i=0;i<prepare::Features.size();++i)EnableWindow(GetDlgItem(window,FeatureFirst+int(i)),enabled&&!source.empty());
        SetWindowTextW(GetDlgItem(window,CancelButton),enabled||action!=StartButton?L"Close":L"Cancel");
    }
    void Paths(){SetWindowTextW(sourceText,source.empty()?L"Select your installed game":source.c_str());SetWindowTextW(outputText,settings.cache.c_str());SetWindowTextW(assetsText,settings.assets.c_str());}
    void Choices(){for(size_t i=0;i<prepare::Features.size();++i)SendDlgItemMessageW(window,FeatureFirst+int(i),BM_SETCHECK,settings.enabled[i]?BST_CHECKED:BST_UNCHECKED,0);Paths();}
    void Reload(){auto loaded=prepare::ReadSettings(source);settings=loaded;Choices();}
    void Begin(int requested){
        if(busy||source.empty())return;
        if(requested==StartButton||requested==InstallButton||requested==PlayButton)for(size_t i=0;i<prepare::Features.size();++i)settings.enabled[i]=SendDlgItemMessageW(window,FeatureFirst+int(i),BM_GETCHECK,0,0)==BST_CHECKED;
        action=requested;if(worker.joinable())worker.join();cancel=false;busy=true;closeAfter=false;started=GetTickCount64();
        {std::lock_guard<std::mutex> lock(mutex);progress={};result={};error.clear();message.clear();}
        Enable(false);SetWindowTextW(status,L"Checking selected operation...");Detail(L"Preparation can be cancelled safely. Installation actions finish before closing.");SendMessageW(bar,PBM_SETPOS,0,0);
        const auto game=source;const auto chosen=settings;
        worker=std::thread([this,game,chosen,requested]{
            try{
                std::wstring done;
                if(requested==RollbackButton)done=prepare::RollbackGame(game);
                else if(requested==UninstallButton){prepare::UninstallGame(game);done=L"Patch uninstalled; settings and prepared data retained.";}
                else if(requested==RecoverButton){prepare::RecoverGame(game);done=L"Interrupted update recovered; installed files verified.";}
                else if(requested==InstallButton||requested==PlayButton)done=prepare::ApplySettings(game,chosen)?L"Patch and settings applied.":L"Patch and settings are already up to date.";
                else {auto completed=prepare::Run(game,chosen,[this](const prepare::Progress& p){std::lock_guard<std::mutex> lock(mutex);progress=p;},[this]{return cancel.load();});std::lock_guard<std::mutex> lock(mutex);result=completed;}
                std::lock_guard<std::mutex> lock(mutex);message=done;
            }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);error=Wide(e.what());}
            PostMessageW(window,Finished,0,0);
        });
    }
    void SelectSource(){
        fs::path selected;if(!Pick(window,L"Select your installed Megadimension Neptunia VII folder",selected))return;
        try{auto game=prepare::GameFolder(selected);auto loaded=prepare::ReadSettings(game);source=game;settings=loaded;Choices();Enable(true);SetWindowTextW(status,L"Ready to install or prepare");Detail(L"Install / Update saves these choices. Prepare / Resume also extracts assets and builds textures.");}catch(const std::exception& e){Error(e);}
    }
};
HWND Control(App& a,const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0){
    HWND control=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,a.Px(x),a.Px(y),a.Px(width),a.Px(height),a.window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(a.font),TRUE);return control;
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    App* a=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
    if(!a)return DefWindowProcW(window,message,w,l);
    switch(message){
    case WM_CREATE:{
        a->font=CreateFontW(-a->Px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        a->heading=CreateFontW(-a->Px(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto title=Control(*a,L"STATIC",L"VII Speedrun Patch",0,22,18,730,32);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(a->heading),TRUE);
        Control(*a,L"STATIC",L"Choose your settings, install the patch, and prepare textures before you play.",0,22,54,736,24);
        Control(*a,L"BUTTON",L"Game folder...",WS_TABSTOP,22,90,144,28,SourceButton);a->sourceText=Control(*a,L"STATIC",L"",SS_PATHELLIPSIS,178,94,455,24);
        Control(*a,L"BUTTON",L"Reload settings",WS_TABSTOP,644,90,114,28,ReloadButton);
        Control(*a,L"BUTTON",L"Cache folder...",WS_TABSTOP,22,130,144,28,OutputButton);a->outputText=Control(*a,L"STATIC",L"",SS_PATHELLIPSIS,178,134,580,24);
        Control(*a,L"BUTTON",L"Unpacked assets...",WS_TABSTOP,22,170,144,28,AssetsButton);a->assetsText=Control(*a,L"STATIC",L"",SS_PATHELLIPSIS,178,174,580,24);
        Control(*a,L"STATIC",L"Loading improvements",0,22,216,360,24);Control(*a,L"STATIC",L"Optional gameplay changes",0,400,216,358,24);
        for(size_t i=0;i<prepare::Features.size();++i)Control(*a,L"BUTTON",prepare::Features[i].label,WS_TABSTOP|BS_AUTOCHECKBOX,i<3?22:400,242+26*int(i<3?i:i-3),358,24,FeatureFirst+int(i));
        Control(*a,L"STATIC",L"Advanced INI settings are preserved. Gameplay changes are off by default.",0,22,346,736,24);
        Control(*a,L"BUTTON",L"Install / Update",WS_TABSTOP|BS_DEFPUSHBUTTON,22,382,194,34,InstallButton);Control(*a,L"BUTTON",L"Prepare / Resume",WS_TABSTOP,226,382,194,34,StartButton);
        Control(*a,L"BUTTON",L"Play",WS_TABSTOP,430,382,132,34,PlayButton);Control(*a,L"BUTTON",L"Close",WS_TABSTOP,646,382,112,34,CancelButton);
        a->status=Control(*a,L"STATIC",L"Ready to install or prepare",0,22,432,736,24,301);a->bar=Control(*a,PROGRESS_CLASSW,L"",0,22,462,736,18);SendMessageW(a->bar,PBM_SETRANGE32,0,1000);
        a->detail=Control(*a,L"EDIT",L"Install / Update saves your choices without preparing assets. Play applies them and starts the game.",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,22,490,736,66,302);
        Control(*a,L"BUTTON",L"Rollback last update",WS_TABSTOP,22,572,234,30,RollbackButton);Control(*a,L"BUTTON",L"Uninstall patch",WS_TABSTOP,273,572,234,30,UninstallButton);Control(*a,L"BUTTON",L"Recover interrupted update",WS_TABSTOP,524,572,234,30,RecoverButton);
        a->Choices();a->Enable(true);if(a->source.empty())SetWindowTextW(a->status,L"Select your game folder to begin");SetTimer(window,1,150,nullptr);return 0;
    }
    case WM_COMMAND:{
        const int id=LOWORD(w);
        if(id==CancelButton){if(a->busy){if(a->action==StartButton){a->cancel=true;SetWindowTextW(a->status,L"Cancelling...");}else{a->closeAfter=true;SetWindowTextW(a->status,L"Finishing safely before closing...");}}else DestroyWindow(window);return 0;}
        if(a->busy)return 0;
        if(id==SourceButton)a->SelectSource();
        else if(id==OutputButton||id==AssetsButton){fs::path selected;if(Pick(window,id==OutputButton?L"Select the texture cache folder":L"Select the unpacked asset folder",selected)){(id==OutputButton?a->settings.cache:a->settings.assets)=selected;a->Paths();}}
        else if(id==ReloadButton){try{a->Reload();SetWindowTextW(a->status,L"Settings reloaded from disk");a->Detail(L"The controls now show the saved settings.");}catch(const std::exception& e){a->Error(e);}}
        else if(id==StartButton||id==InstallButton||id==PlayButton||id==RollbackButton||id==UninstallButton||id==RecoverButton)a->Begin(id);return 0;
    }
    case WM_TIMER:
        if(a->busy&&!a->cancel){std::lock_guard<std::mutex> lock(a->mutex);const auto& p=a->progress;if(p.totalFiles){auto text=L"Preparing "+std::to_wstring(p.files)+L" / "+std::to_wstring(p.totalFiles)+L" files - "+std::to_wstring((GetTickCount64()-a->started)/1000)+L" seconds elapsed";SetWindowTextW(a->status,text.c_str());a->Detail(p.current);SendMessageW(a->bar,PBM_SETPOS,p.totalBytes?WPARAM(p.bytes*1000/p.totalBytes):0,0);}}return 0;
    case Finished:{
        if(a->worker.joinable())a->worker.join();a->busy=false;a->Enable(true);
        if(a->error.empty()){
            try{
                a->Reload();
                if(a->action==PlayButton&&!a->closeAfter){auto exe=a->source/L"NeptuniaVII.exe";if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",exe.c_str(),nullptr,a->source.c_str(),SW_SHOWNORMAL))<=32)throw std::runtime_error("Settings applied, but the game could not start");a->message=L"Patch applied; game started.";}
                SetWindowTextW(a->status,a->message.empty()?L"Preparation complete":a->message.size()>90?L"Action complete; see details below":a->message.c_str());
                if(a->action==StartButton)a->Detail(std::to_wstring(a->result.generated)+L" textures built, "+std::to_wstring(a->result.reused)+L" reused; "+std::to_wstring(a->result.unsupported)+L" unsupported sources will use the original loader.");
                else a->Detail(a->message+L"\r\nSaved settings are shown above. Prepared assets, texture caches and logs are retained.");SendMessageW(a->bar,PBM_SETPOS,1000,0);
            }catch(const std::exception& e){a->Error(e);}
        }else{SetWindowTextW(a->status,a->cancel?L"Cancelled; completed files can be reused":L"Action stopped; see details below");a->Detail(a->error);}
        if(a->closeAfter)DestroyWindow(window);return 0;
    }
    case WM_CLOSE:if(a->busy){a->cancel=a->action==StartButton;a->closeAfter=true;SetWindowTextW(a->status,L"Finishing safely before closing...");}else DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
int Gui(const fs::path& source,const prepare::Settings& settings){
    SetProcessDPIAware();const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);App app;app.source=source;app.settings=settings;auto dc=GetDC(nullptr);app.dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(nullptr,dc);
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=WindowProc;c.lpszClassName=L"VII_PREPARE_GAME";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&c);
    RECT bounds{0,0,app.Px(780),app.Px(624)};AdjustWindowRect(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    auto window=CreateWindowW(c.lpszClassName,L"VII Prepare Game",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,c.hInstance,&app);
    if(!window){CoUninitialize();return 1;}ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return 0;
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
            }else if(arg==L"--help"){Print("VII Prepare Game: --source GAME prepares/resumes. --install (alias --enable-only) applies without preparation. --output CACHE, --assets FOLDER and --set Feature=0|1 choose settings. --settings prints saved settings as JSON. --if-config-hash HASH refuses stale settings (empty means absent INI). --gui opens the window without starting work. --rollback, --uninstall or --recover maintain installed files and retain prepared data.");LocalFree(argv);return 0;}else throw std::runtime_error("Unknown or incomplete command-line option");
        }
        LocalFree(argv);argv=nullptr;const int actions=int(recover)+int(rollback)+int(uninstall)+int(install)+int(query);const bool overrides=!output.empty()||!assets.empty()||!changes.empty()||hasExpected;
        if(actions>1||(gui&&actions)||((recover||rollback||uninstall||query)&&overrides))throw std::runtime_error("Select one action; settings overrides apply only to install, prepare or the window");
        if(source.empty())source=prepare::DetectGame();if(!source.empty())source=prepare::GameFolder(source);if(source.empty()&&!gui)throw std::runtime_error("Game not detected; supply --source GAME");
        if(rollback){Print(Utf8(prepare::RollbackGame(source)));return 0;}if(uninstall){prepare::UninstallGame(source);Print("Patch uninstalled; settings and prepared data retained.");return 0;}if(recover){prepare::RecoverGame(source);Print("Interrupted update recovered; installed files verified.");return 0;}
        auto settings=source.empty()?prepare::Settings{}:prepare::ReadSettings(source);if(!output.empty())settings.cache=fs::absolute(output).lexically_normal();if(!assets.empty())settings.assets=fs::absolute(assets).lexically_normal();for(const auto& change:changes)settings.enabled[change.first]=change.second;if(hasExpected)settings.configHash=expectedHash;
        if(gui)return Gui(source,settings);
        if(query){std::string result="{\"cache\":"+Json(Utf8(settings.cache.wstring()))+",\"assets\":"+Json(Utf8(settings.assets.wstring()))+",\"config_sha256\":"+Json(settings.configHash)+",\"features\":{";for(size_t i=0;i<prepare::Features.size();++i){if(i)result+=',';result+=Json(Utf8(prepare::Features[i].key))+":"+(settings.enabled[i]?"true":"false");}Print(result+"}}");return 0;}
        if(install){Print(prepare::ApplySettings(source,settings)?"Patch and settings applied.":"Patch and settings are already up to date.");return 0;}
        uint64_t last=UINT64_MAX;auto report=[&](const prepare::Progress& p){if(p.files/1000!=last){last=p.files/1000;Print("{\"progress_files\":"+std::to_string(p.files)+",\"total_files\":"+std::to_string(p.totalFiles)+",\"bytes\":"+std::to_string(p.bytes)+"}");}};
        auto result=prepare::Run(source,settings,report);Print("{\"complete\":true,\"generated\":"+std::to_string(result.generated)+",\"reused\":"+std::to_string(result.reused)+",\"unsupported\":"+std::to_string(result.unsupported)+"}");return 0;
    }catch(const std::exception& e){if(argv)LocalFree(argv);Print("{\"error\":"+Json(e.what())+"}");if(gui)MessageBoxW(nullptr,Wide(e.what()).c_str(),L"VII Prepare Game",MB_OK|MB_ICONERROR);return 1;}
}
