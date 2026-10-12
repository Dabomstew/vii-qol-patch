#include "patch.hpp"
#include "early_graphics.hpp"
#include <mutex>
#include <unknwn.h>
static HMODULE proxyModule=nullptr;
static HMODULE realModule=nullptr;
static std::once_flag realOnce,patchOnce;
static void InitializePatches() noexcept {
    std::call_once(patchOnce,[]{vii::Initialize(proxyModule);});
}
static void InitializeGraphics() noexcept {
    try {
        const auto path=vii::ModulePath(proxyModule);
        const auto ini=path.substr(0,path.find_last_of(L"\\/"))+L"\\vii-patches.ini";
        if (!GetPrivateProfileIntW(L"Patches",L"Neptasm",0,ini.c_str())) return;
        for (const auto feature : {L"Resolution", L"FitWindow", L"Ultrawide", L"WindowControl"})
            if (GetPrivateProfileIntW(L"Neptasm",feature,0,ini.c_str())) {InitializePatches(); return;}
    } catch (...) {} // Retain the original DirectInput initialization fallback.
}
static FARPROC Resolve(const char* name) {
    std::call_once(realOnce,[] {
        wchar_t directory[MAX_PATH]; auto n=GetSystemDirectoryW(directory,MAX_PATH);
        if(n && n<MAX_PATH) realModule=LoadLibraryW((std::wstring(directory)+L"\\dinput8.dll").c_str());
    });
    return realModule?GetProcAddress(realModule,name):nullptr;
}
extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance,DWORD version,REFIID iid,void** output,IUnknown* outer) {
    using Fn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto function=reinterpret_cast<Fn>(Resolve("DirectInput8Create"));
    if(!function) return E_FAIL;
    InitializePatches();
    return function(instance,version,iid,output,outer);
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {return S_FALSE;}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid,REFIID iid,void** output) {
    using Fn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**); auto fn=reinterpret_cast<Fn>(Resolve("DllGetClassObject")); return fn?fn(clsid,iid,output):E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {using Fn=HRESULT(WINAPI*)();auto fn=reinterpret_cast<Fn>(Resolve("DllRegisterServer"));return fn?fn():E_FAIL;}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {using Fn=HRESULT(WINAPI*)();auto fn=reinterpret_cast<Fn>(Resolve("DllUnregisterServer"));return fn?fn():E_FAIL;}
extern "C" const void* WINAPI ProxyGetdfDIJoystick() {using Fn=const void*(WINAPI*)();auto fn=reinterpret_cast<Fn>(Resolve("GetdfDIJoystick"));return fn?fn():nullptr;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        proxyModule=instance;
        vii::InstallEarlyGraphicsBootstrap(GetModuleHandleW(nullptr),InitializeGraphics);
    }
    return TRUE;
}
