// Synthetic x86 adapter tests; no game executable or assets are used.
#include "patch.hpp"
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
static unsigned qpcCalls=0,logCalls=0,originalCalls=0,checks=0;
static BOOL WINAPI CountQpc(LARGE_INTEGER* q) {++qpcCalls;return QueryPerformanceCounter(q);}
#include "../src/ordered_event_keys.cpp"
#define QueryPerformanceCounter CountQpc
#include "../src/mip_cache.cpp"
#undef QueryPerformanceCounter
namespace vii {
void Log(const char*,...) noexcept {++logCalls;}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback){return fallback;}
bool ReplaceImport(const Context&,uintptr_t,void*,void*){return false;}
}
using namespace vii;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static HRESULT WINAPI Original(ID3D11Device*,const void*,SIZE_T,TextureLoadInfo*,void*,ID3D11Resource**,HRESULT*) {
    ++originalCalls;return E_FAIL;
}
static void HashProvider() {
    std::atomic<bool> start{false},valid{true};std::vector<std::thread> threads;
    for(unsigned i=0;i<4;++i)threads.emplace_back([&]{
        while(!start.load())YieldProcessor();
        for(unsigned n=0;n<200;++n)if(TextureDigestText(Hash("abc",3))!=
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")valid=false;
    });
    start=true;for(auto& worker:threads)worker.join();CHECK(valid.load());
    CHECK(TextureDigestText(Hash("",0))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
static void RequestAndModalDiagnostics(const std::filesystem::path& root) {
    State cache;unsigned char source[160]{};
    qpcCalls=logCalls=0;
    {Request request(cache,0,source,sizeof(source),nullptr);CHECK(request.id==0&&request.start.QuadPart==0);}
    CHECK(qpcCalls==0&&cache.sequence==0);
    cache.trace=CreateFileW((root/L"trace.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
    CHECK(cache.trace!=INVALID_HANDLE_VALUE);
    {Request request(cache,0,source,sizeof(source),nullptr);CHECK(request.id==1);request.event="test";}
    CHECK(qpcCalls==2&&cache.sequence==1);
    CloseHandle(cache.trace);cache.trace=INVALID_HANDLE_VALUE;
    std::ifstream input(root/L"trace.log");std::string lines((std::istreambuf_iterator<char>(input)),{});
    CHECK(lines.find("begin id=1")!=std::string::npos&&lines.find("end id=1")!=std::string::npos);
    trace=false;queue.Open(0);owner=reinterpret_cast<void*>(1);scene=42;Close();
    CHECK(logCalls==0&&!queue.IsOpen()&&owner==nullptr);
    trace=true;queue.Open(0);owner=reinterpret_cast<void*>(1);scene=42;Close();
    CHECK(logCalls==1&&!queue.IsOpen()&&owner==nullptr);trace=false;
}
static void TextureHitDiagnostics(const std::filesystem::path& root) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context)));
    std::vector<unsigned char> source(160);
    auto word=[&](size_t at,uint32_t value){std::memcpy(source.data()+at,&value,4);};
    word(0,0x20534444);word(4,124);word(12,8);word(16,8);word(28,1);word(76,32);word(84,0x31545844);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_BC1_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    State cache;cache.original=Original;cache.diskMode=1;cache.diskStarted=true;
    Digest identity{};identity[0]=17;
    CHECK(cache.disk[0].Open((root/L"cache").wstring(),identity));
    CHECK(cache.disk[0].Write(source,source,desc));state=&cache;
    // A real stdcall thunk gives Create its qualified return RVA, without
    // production-only seams or a real game module. Seven arguments are copied.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(code!=nullptr);
    for(unsigned i=0;i<7;++i){const unsigned char push[]={0xff,0x74,0x24,0x1c};std::memcpy(code+4*i,push,4);}
    code[28]=0xe8;const uint32_t relative=uint32_t(reinterpret_cast<uintptr_t>(Create)-reinterpret_cast<uintptr_t>(code+33));
    std::memcpy(code+29,&relative,4);code[33]=0xc2;code[34]=0x1c;code[35]=0;
    DWORD old=0;CHECK(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),code,36));
    cache.context.game=reinterpret_cast<HMODULE>(reinterpret_cast<uintptr_t>(code+33)-0x38ba10);
    auto invoke=reinterpret_cast<TextureCreateFn>(code);TextureLoadInfo options;ID3D11Resource* output=nullptr;
    qpcCalls=logCalls=originalCalls=0;
    CHECK(SUCCEEDED(invoke(device.Get(),source.data(),source.size(),&options,nullptr,&output,nullptr))&&output);
    output->Release();output=nullptr;
    CHECK(cache.diskHits==1&&cache.hits==1&&originalCalls==0&&logCalls==0&&qpcCalls==0&&cache.sequence==0);
    CHECK(SUCCEEDED(invoke(device.Get(),source.data(),source.size(),&options,nullptr,&output,nullptr))&&output);
    output->Release();output=nullptr;
    CHECK(cache.diskHits==1&&cache.hits==2&&logCalls==0&&qpcCalls==0); // RAM replay.
    cache.entries.clear();cache.bytes=0;
    cache.trace=CreateFileW((root/L"hits.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
    CHECK(cache.trace!=INVALID_HANDLE_VALUE);
    CHECK(SUCCEEDED(invoke(device.Get(),source.data(),source.size(),&options,nullptr,&output,nullptr))&&output);
    output->Release();output=nullptr;
    CHECK(cache.diskHits==2&&cache.hits==3&&originalCalls==0&&logCalls==1&&qpcCalls==2);
    CloseHandle(cache.trace);cache.trace=INVALID_HANDLE_VALUE;
    CHECK(VirtualFree(code,0,MEM_RELEASE));state=nullptr;
}
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==2);const auto root=std::filesystem::absolute(argv[1]);
    CHECK(std::filesystem::create_directories(root));
    HashProvider();RequestAndModalDiagnostics(root);TextureHitDiagnostics(root);
    std::printf("PASS %u checks: trace-off/on request clocks and IDs, ordered modal logs, real x86 disk/RAM upload and logging\n",checks);
}
