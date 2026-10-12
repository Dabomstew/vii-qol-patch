#include "../src/early_graphics.cpp"
#include <cstdio>
#include <cstdlib>
using namespace vii;
static unsigned checks=0, initialized=0, forwarded=0;
#define CHECK(x) do {++checks; if (!(x)) {std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(false)
static HRESULT WINAPI Original(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,
    const DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**) {
    ++forwarded;return S_FALSE;
}
static HRESULT WINAPI Replacement(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,
    const DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**) {
    CHECK(initialized==1);++forwarded;return S_OK;
}
static void Enabled() noexcept {++initialized;CHECK(*creationSlot==reinterpret_cast<void*>(Original));*creationSlot=reinterpret_cast<void*>(Replacement);}
static void Disabled() noexcept {++initialized;}
static HRESULT Invoke() {return Bootstrap(nullptr,D3D_DRIVER_TYPE_NULL,nullptr,0,nullptr,0,0,nullptr,nullptr,nullptr,nullptr,nullptr);}
int main() {
    auto* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(bytes);
    const auto image=reinterpret_cast<HMODULE>(bytes);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=256;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes+256);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt->OptionalHeader.SizeOfImage=4096;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={1024,40};
    auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes+1024);
    imports->Name=1200;imports->OriginalFirstThunk=1300;imports->FirstThunk=1400;
    std::memcpy(bytes+1200,"d3d11.dll",10);*reinterpret_cast<DWORD*>(bytes+1300)=1500;
    std::memcpy(bytes+1502,"D3D11CreateDeviceAndSwapChain",sizeof("D3D11CreateDeviceAndSwapChain"));
    auto** slot=reinterpret_cast<void**>(bytes+1400);
    CHECK(FindCreationSlot(image)==slot);CHECK(!FindCreationSlot(nullptr));
    imports->Name=4095;CHECK(!FindCreationSlot(image));imports->Name=1200;
    imports->OriginalFirstThunk=4095;CHECK(!FindCreationSlot(image));imports->OriginalFirstThunk=1300;
    *reinterpret_cast<DWORD*>(bytes+1300)=IMAGE_ORDINAL_FLAG32|1;CHECK(!FindCreationSlot(image));
    *reinterpret_cast<DWORD*>(bytes+1300)=1500;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=4096;CHECK(!FindCreationSlot(image));
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=40;
    *slot=reinterpret_cast<void*>(Original);CHECK(!InstallEarlyGraphicsBootstrap(image,Enabled));CHECK(*slot==reinterpret_cast<void*>(Original));
    creationSlot=slot;originalCreation=Original;initializeGraphics=Enabled;*slot=reinterpret_cast<void*>(Bootstrap);
    CHECK(Invoke()==S_OK);CHECK(initialized==1&&forwarded==1);CHECK(*slot==reinterpret_cast<void*>(Replacement));
    initialized=forwarded=0;initializeGraphics=Disabled;*slot=reinterpret_cast<void*>(Bootstrap);
    CHECK(Invoke()==S_FALSE);CHECK(initialized==1&&forwarded==1);CHECK(*slot==reinterpret_cast<void*>(Original));
    initialized=forwarded=0;*slot=reinterpret_cast<void*>(Replacement);
    CHECK(Invoke()==S_FALSE);CHECK(initialized==0&&forwarded==1);CHECK(*slot==reinterpret_cast<void*>(Replacement));
    CHECK(VirtualFree(bytes,0,MEM_RELEASE));
    std::printf("PASS %u checks: bounded import discovery, pre-creation dispatch and unchanged fallback\n",checks);
}
