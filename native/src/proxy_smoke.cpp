#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    auto dll=LoadLibraryW(argv[1]);if(!dll){printf("LoadLibrary failed %lu\n",GetLastError());return 3;}
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto create=reinterpret_cast<Create>(GetProcAddress(dll,"DirectInput8Create"));if(!create)return 4;
    IDirectInput8W* input=nullptr;auto hr=create(GetModuleHandleW(nullptr),0x0800,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr);
    printf("DirectInput8Create HRESULT=%08lx object=%p\n",hr,input);
    if(input)input->Release();
    auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
    bool ok=SUCCEEDED(hr) && input && unload && unload()==S_FALSE;
    using Stats=int(__cdecl*)(void*,unsigned);
    auto stats=reinterpret_cast<Stats>(GetProcAddress(dll,"VIIGetMotionCacheStats"));
    unsigned char snapshot[160]{};
    ok=ok && stats && stats(snapshot,sizeof(snapshot))==0 && stats(nullptr,0)==0;
    FreeLibrary(dll);return ok?0:5;
}
