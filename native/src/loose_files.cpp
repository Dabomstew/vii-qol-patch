#include "patch.hpp"
#include "code_calls.hpp"
#include "loose_manifest.hpp"
#include "game_path.hpp"
#include "pac_archive.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace vii {
namespace {
namespace fs = std::filesystem;
using OpenFn = char(__thiscall*)(void*,const char*,uint32_t*);
using NewFn = void*(__cdecl*)(size_t);
using DeleteFn = void(__cdecl*)(void*);
using AppendFn = void(__thiscall*)(void*,void*);
struct LooseState {
    fs::path root,dlcRoot;
    std::unordered_map<std::string,loose::File> files;
    bool verify = false;
    NewFn allocate = nullptr;
    DeleteFn release = nullptr;
    AppendFn append = nullptr;
};
std::unique_ptr<LooseState> looseState;
OpenFn originalOpen = nullptr;
std::atomic<bool> looseEnabled{false};
std::atomic<uint64_t> opens{0}, hits{0}, misses{0}, errors{0}, sourceRejects{0}, verified{0}, mismatches{0};
struct LooseStats {
    uint32_t size=sizeof(LooseStats), version=1, enabled=0, reserved=0;
    uint64_t opens,hits,misses,errors,sourceRejects,verified,mismatches;
};
static_assert(sizeof(LooseStats)==72);
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    HANDLE Release(){auto value=h;h=INVALID_HANDLE_VALUE;return value;}
};
template<class T> T& Field(void* p,size_t offset){return *reinterpret_cast<T*>(static_cast<char*>(p)+offset);}
struct ManagerLock {
    CRITICAL_SECTION* lock;
    explicit ManagerLock(void* m):lock(reinterpret_cast<CRITICAL_SECTION*>(static_cast<char*>(m)+0x38)){EnterCriticalSection(lock);}
    ~ManagerLock(){LeaveCriticalSection(lock);}
};
void AddLooseManifest(LooseState& state,const fs::path& root,const fs::path& sources,const std::string& prefix){
    loose::CheckOutputPath(root,L"manifest.vii");
    auto manifest=loose::ReadManifest(root/L"manifest.vii");
    std::unordered_set<std::string> rejected;
    for(const auto& source:manifest.sources)if(!loose::Matches(sources/fs::path(source.name),source)){
        rejected.insert(source.group);++sourceRejects;
        Log("LooseFiles source identity rejected: %s",source.name.c_str());
    }
    for(auto& file:manifest.files){const auto& group=manifest.sources[file.source].group;
        if(!rejected.count(group))state.files.emplace(prefix+group+"/"+file.name,std::move(file));
    }
}
std::unique_ptr<LooseState> LoadLooseState(const fs::path& root,const fs::path& contents,bool verify){
    auto state=std::make_unique<LooseState>();state->root=root;state->dlcRoot=loose::DlcOutput(root);state->verify=verify;
    AddLooseManifest(*state,root,contents,"contents/");
    const auto dlc=contents.parent_path()/L"DLC";
    if(fs::is_directory(dlc)&&fs::exists(state->dlcRoot/L"manifest.vii")){
        // DLC is optional. A stale/corrupt DLC extraction must not disable
        // base-game loose loading; original PAC loading remains the fallback.
        try{AddLooseManifest(*state,state->dlcRoot,dlc,"dlc/");}
        catch(const std::exception& e){Log("LooseFiles DLC unavailable: %s",e.what());}
    }
    if(state->files.empty())throw std::runtime_error("No loose files match installed PAC identities");
    return state;
}
bool OpenLoose(LooseState& state,void* manager,const char* name,uint32_t* out){
    if(!manager||!name||!out)return false;
    const auto length=strnlen_s(name,260);if(!length||length>=260)return false;
    const auto space=Field<const char*>(manager,0x30);
    if(!space||strnlen_s(space,1024)>=1024)return false;
    const auto key=pac::ManagerNamespace(space)+"/"+pac::NormalizePath(name,true);
    const auto found=state.files.find(key);if(found==state.files.end())return false;
    const bool dlc=key.rfind("dlc/",0)==0;const auto& root=dlc?state.dlcRoot:state.root;
    const auto relative=fs::path(key.substr(dlc?4:9));loose::CheckOutputPath(root,relative);
    Handle file{CreateFileW(loose::Extended(root/relative).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(file.h==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        ((uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow)!=found->second.size){++mismatches;return false;}
    if(state.verify){
        loose::Sha256 hash;std::vector<unsigned char> buffer(1024*1024);uint64_t total=0;
        for(;;){DWORD got=0;if(!ReadFile(file.h,buffer.data(),DWORD(buffer.size()),&got,nullptr))throw std::runtime_error("Loose file verification read failed");
            if(!got)break;hash.Add(buffer.data(),got);total+=got;}
        if(total!=found->second.size||hash.Finish()!=found->second.hash){++mismatches;return false;}
        LARGE_INTEGER zero{};if(!SetFilePointerEx(file.h,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Loose file verification rewind failed");++verified;
    }
    ManagerLock guard(manager);
    auto begin=Field<void**>(manager,0xc),end=Field<void**>(manager,0x10);
    const size_t count=begin?size_t(end-begin):0;uint32_t index=0;
    for(;index<count;++index)if(!Field<unsigned char>(begin[index],8))break;
    void* entry=nullptr;
    if(index==count){
        entry=state.allocate(0x140);if(!entry)throw std::bad_alloc();std::memset(entry,0,0x140);
        try{state.append(static_cast<char*>(manager)+0xc,&entry);}catch(...){state.release(entry);throw;}
    }else{
        entry=begin[index];
        if(Field<void*>(entry,0x134)||Field<void*>(entry,0x138))throw std::runtime_error("Loose slot retains decoder buffers");
    }
    std::memset(entry,0,0x140);Field<HANDLE>(entry,4)=file.Release();Field<unsigned char>(entry,8)=1;
    std::memcpy(static_cast<char*>(entry)+0x14,name,length+1);
    Field<uint32_t>(entry,0x11c)=uint32_t(found->second.size);Field<uint32_t>(entry,0x120)=uint32_t(found->second.size);
    Field<int32_t>(entry,0x13c)=-1;Field<uint32_t>(manager,0x34)=0;*out=index;return true;
}
char __fastcall NativeLooseOpen(void* manager,void*,const char* name,uint32_t* out){
    if(looseEnabled.load(std::memory_order_acquire)){
        ++opens;
        try{if(OpenLoose(*looseState,manager,name,out)){++hits;return 1;}}catch(...){++errors;}
        ++misses;
    }
    return originalOpen(manager,name,out);
}
}
bool InstallLooseFiles(const Context& context){
    try{
        wchar_t configured[32768]{};
        const auto n=GetPrivateProfileStringW(L"LooseFiles",L"Directory",L"vii-speedrun-patch\\unpacked",configured,DWORD(std::size(configured)),context.ini.c_str());
        if(n>=std::size(configured)-1)throw std::runtime_error("Invalid loose directory");
        fs::path root=ResolveGamePath(context.directory,configured,L"vii-speedrun-patch\\unpacked");
        auto state=LoadLooseState(root,fs::path(context.directory)/L"CONTENTS",Option(context,L"LooseFiles",L"Verify",0)!=0);
        auto base=reinterpret_cast<unsigned char*>(context.game),entry=base+0x467bd0;
        const unsigned char expected[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0xd2,0xb7,0x8b,0x00,0x64,0xa1,0,0,0,0};
        auto signature=std::array<unsigned char,sizeof(expected)>{};std::memcpy(signature.data(),expected,sizeof(expected));
        const uint32_t handler=uint32_t(reinterpret_cast<uintptr_t>(base)+0x4bb7d2);std::memcpy(signature.data()+6,&handler,4);
        if(std::memcmp(entry,signature.data(),signature.size()))throw std::runtime_error("Loose open entry signature mismatch");
        state->allocate=reinterpret_cast<NewFn>(base+0x453416);state->append=reinterpret_cast<AppendFn>(base+0x46a320);
        state->release=reinterpret_cast<DeleteFn>(GetProcAddress(GetModuleHandleW(L"MSVCR120.dll"),"??3@YAXPAX@Z"));
        if(!state->release)throw std::runtime_error("Game allocator cleanup unavailable");
        auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!trampoline)throw std::bad_alloc();
        std::memcpy(trampoline,entry,5);trampoline[5]=0xe9;
        const uint32_t relative=uint32_t(reinterpret_cast<uintptr_t>(entry+5)-reinterpret_cast<uintptr_t>(trampoline+10));std::memcpy(trampoline+6,&relative,4);
        DWORD old=0;
        if(!VirtualProtect(trampoline,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),trampoline,10)){
            VirtualFree(trampoline,0,MEM_RELEASE);throw std::runtime_error("Loose trampoline protection failed");
        }
        // Publish before patching; a peer entering during commit safely calls original.
        originalOpen=reinterpret_cast<OpenFn>(trampoline);looseState=std::move(state);
        CallSite site{entry,{},reinterpret_cast<void*>(NativeLooseOpen)};std::memcpy(site.expected.data(),entry,8);
        const auto report=ReplaceEntryJump(site);
        const bool ready=report.installed&&report.status==CallStatus::Installed;
        looseEnabled.store(ready,std::memory_order_release);
        Log("LooseFiles hook=%s files=%zu verify=%d peers=%u rollback=%d",CallStatusName(report.status),looseState->files.size(),looseState->verify,report.peers,report.rolledBack);
        // Keep initialized state and trampoline pinned even after uncertain writes.
        return ready;
    }catch(const std::exception& e){Log("LooseFiles unavailable: %s",e.what());return false;}
}
extern "C" __declspec(dllexport) uint32_t __cdecl VIIGetLooseFileStats(void* output,uint32_t size){
    if(!output||size!=sizeof(LooseStats))return 0;
    LooseStats s{sizeof(LooseStats),1,looseEnabled.load()?1u:0u,0,opens.load(),hits.load(),misses.load(),errors.load(),sourceRejects.load(),verified.load(),mismatches.load()};
    std::memcpy(output,&s,size);return 1;
}
}
