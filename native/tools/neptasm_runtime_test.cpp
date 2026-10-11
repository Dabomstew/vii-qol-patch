#include "../src/neptasm.cpp"
#include <cstdio>
#include <cstdlib>
namespace vii {
void Log(const char*,...) noexcept {}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback){return fallback;}
bool ReplaceImport(const Context&,uintptr_t,void*,void*){return false;}
}
using namespace vii;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static HRESULT STDMETHODCALLTYPE Description(IDXGISwapChain*,DXGI_SWAP_CHAIN_DESC* desc){
    *desc={};desc->BufferDesc.Width=1920;desc->BufferDesc.Height=1080;return S_OK;
}
static void MinimalComHooks(){
    State graphics;state=&graphics;
    void* deviceTable[43],*contextTable[51],*swapTable[18];
    for(auto& slot:deviceTable)slot=reinterpret_cast<void*>(1);
    for(auto& slot:contextTable)slot=reinterpret_cast<void*>(2);
    for(auto& slot:swapTable)slot=reinterpret_cast<void*>(3);
    swapTable[12]=reinterpret_cast<void*>(Description);
    struct Object {void** table;};Object device{deviceTable},context{contextTable},swap{swapTable};
    CHECK(InstallComHooks(reinterpret_cast<IDXGISwapChain*>(&swap),reinterpret_cast<ID3D11Device*>(&device),reinterpret_cast<ID3D11DeviceContext*>(&context)));
    CHECK(swap.table==swapTable);
    for(unsigned i=0;i<43;++i)CHECK(i==5?device.table[i]!=deviceTable[i]:device.table[i]==deviceTable[i]);
    for(unsigned i=0;i<51;++i)CHECK(i==44?context.table[i]!=contextTable[i]:context.table[i]==contextTable[i]);
    CHECK(VirtualFree(graphics.deviceVtable,0,MEM_RELEASE));CHECK(VirtualFree(graphics.contextVtable,0,MEM_RELEASE));state=nullptr;
}
int main() {
    State image;CHECK(!ReadImageRange(image));
    auto* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(bytes);image.context.game=reinterpret_cast<HMODULE>(bytes);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes+256);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=256;
    nt->Signature=IMAGE_NT_SIGNATURE;nt->OptionalHeader.SizeOfImage=4096;
    CHECK(ReadImageRange(image));state=&image;
    const auto begin=reinterpret_cast<uintptr_t>(bytes);
    CHECK(!InGame(begin-1)&&InGame(begin)&&InGame(begin+4095)&&!InGame(begin+4096));
    dos->e_magic=0;CHECK(InGame(begin+128)); // Hot path uses validated range.
    CHECK(!ReadImageRange(image));dos->e_magic=IMAGE_DOS_SIGNATURE;
    nt->Signature=0;CHECK(!ReadImageRange(image));nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.SizeOfImage=0;CHECK(!ReadImageRange(image));
    dos->e_lfanew=0x7fffffff;CHECK(!ReadImageRange(image)); // Guarded malformed read.
    state=nullptr;CHECK(!InGame(begin));CHECK(VirtualFree(bytes,0,MEM_RELEASE));
    MinimalComHooks();
    std::printf("PASS %u checks: cached graphics caller range, invalid headers and only necessary COM hooks\n",checks);
}
