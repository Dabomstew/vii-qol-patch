#include "patch.hpp"
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
namespace vii {
static std::wstring logPath;
static std::mutex logMutex;
void Log(const char* format, ...) noexcept {
    try {
        std::lock_guard<std::mutex> lock(logMutex);
        if(logPath.empty()) return;
        char message[1024]; va_list args; va_start(args,format); vsnprintf_s(message,sizeof(message),_TRUNCATE,format,args); va_end(args);
        FILE* file=nullptr;
        if(_wfopen_s(&file,logPath.c_str(),L"ab") || !file) return;
        fprintf(file,"%llu %s\n",GetTickCount64(),message); fclose(file);
    } catch (...) {}
}
int Option(const Context& c,const wchar_t* section,const wchar_t* key,int fallback) {return GetPrivateProfileIntW(section,key,fallback,c.ini.c_str());}
bool ReplaceImport(const Context& c,uintptr_t rva,void* expected,void* replacement) {
    auto slot=reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(c.game)+rva);
    if(*slot!=expected) {Log("IAT mismatch at RVA %08x; leaving existing hook intact",static_cast<unsigned>(rva));return false;}
    DWORD old=0; if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) return false;
    auto prior=InterlockedCompareExchangePointer(slot,replacement,expected);
    DWORD unused=0; VirtualProtect(slot,sizeof(void*),old,&unused);
    return prior==expected;
}
void Initialize(HMODULE proxy) noexcept {
    try {
        auto path=ModulePath(proxy); auto directory=path.substr(0,path.find_last_of(L"\\/"));
        logPath=directory+L"\\vii-patches.log";
        Context context{GetModuleHandleW(nullptr),directory,directory+L"\\vii-patches.ini"};
        const Digest expected={0x7f,0xf2,0xaa,0xd5,0x5e,0x96,0x5a,0xdd,0x3b,0x6f,0xc4,0x5b,0xd0,0xad,0x21,0x58,0x76,0x9d,0xe0,0x14,0xd6,0xf6,0x19,0xdc,0x56,0x4a,0x50,0xc3,0xe0,0xfa,0x42,0xd9};
        if(HashFile(ModulePath(context.game))!=expected) {Log("Unsupported executable SHA256; all patches disabled");return;}
        Log("VII QoL Patch %s", PATCH_RELEASE_VERSION);
        ProbeStartup(context);
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(proxy),&pinned)) throw std::runtime_error("cannot pin proxy");
        const Patch patches[]={{L"MipCache",InstallMipCache},{L"MotionCache",InstallMotionCache,0},
            {L"AutoSkipEvents",InstallAutoSkipEvents,0},{L"BattleAutoSkip",InstallBattleAutoSkip,0},
            {L"EventSkipBuffer",InstallEventSkipBuffer,0},{L"SuppressTutorials",InstallSuppressTutorials,0},
            {L"SuppressDungeonPreviews",InstallSuppressDungeonPreview,0},
            {L"JPBattleBalance",InstallJPBattleBalance,0},
            {L"LooseFiles",InstallLooseFiles,0},{L"Neptasm",InstallNeptasm,0},
            {L"NewGameDetector",InstallNewGameDetector}};
        for(const auto& patch:patches) {
            if(!Option(context,L"Patches",patch.name,patch.defaultEnabled)) {Log("Patch disabled: %ls",patch.name);continue;}
            Log("Patch %ls: %s",patch.name,patch.install(context)?"installed":"not installed");
        }
        Log("LoadTiming: %s",InstallLoadTiming(context)?"installed":"not installed");
    } catch(const std::exception& error) {Log("Initialization failed: %s",error.what());}
    catch(...) {Log("Initialization failed with unknown exception");}
}
}
