#include "../src/loose_files.cpp"
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <winioctl.h>
namespace vii {
void Log(const char*,...) noexcept {}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback){return fallback;}
}
using namespace vii;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static unsigned allocations=0,releases=0,fallbacks=0;
static bool failAppend=false;
static void* __cdecl Allocate(size_t bytes){++allocations;return std::malloc(bytes);}
static void __cdecl Release(void* p){++releases;std::free(p);}
static void* slots[16]{};
static void __fastcall Append(void* vector,void*,void* value){
    if(failAppend)throw std::runtime_error("injected append failure");
    auto end=Field<void**>(vector,4);*end=*static_cast<void**>(value);Field<void**>(vector,4)=end+1;
}
static char __fastcall Fallback(void*,void*,const char*,uint32_t* out){++fallbacks;*out=77;return 1;}
static void Write(const fs::path& path,const std::string& data){std::ofstream f(path,std::ios::binary);f<<data;CHECK(bool(f));}
static void Junction(const fs::path& link,const fs::path& target){
    CHECK(fs::create_directory(link));
    const auto print=fs::absolute(target).wstring(),sub=L"\\??\\"+print;
    const auto subBytes=sub.size()*sizeof(wchar_t),printBytes=print.size()*sizeof(wchar_t);
    std::vector<unsigned char> buffer(16+subBytes+2+printBytes+2);
    const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;std::memcpy(buffer.data(),&tag,4);
    const USHORT fields[]={USHORT(buffer.size()-8),0,0,USHORT(subBytes),USHORT(subBytes+2),USHORT(printBytes)};
    std::memcpy(buffer.data()+4,fields,sizeof(fields));
    std::memcpy(buffer.data()+16,sub.c_str(),subBytes+2);
    std::memcpy(buffer.data()+16+subBytes+2,print.c_str(),printBytes+2);
    HANDLE directory=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);CHECK(directory!=INVALID_HANDLE_VALUE);
    DWORD done=0;const bool ok=DeviceIoControl(directory,FSCTL_SET_REPARSE_POINT,buffer.data(),DWORD(buffer.size()),nullptr,0,&done,nullptr)!=FALSE;
    const auto error=GetLastError();CloseHandle(directory);
    if(!ok)std::fprintf(stderr,"junction fixture creation failed: %lu\n",error);CHECK(ok);
}
int wmain(int argc,wchar_t** argv){
    CHECK(argc==2);fs::path root(argv[1]),contents=root/L"CONTENTS",output=contents/L"vii-unpacked";
    fs::create_directories(output/L"game/model");fs::create_directories(output/L"system");
    Write(contents/L"GAME00000.pac",std::string(32,'p'));Write(contents/L"GAME00001.pac",std::string(32,'q'));
    Write(contents/L"SYSTEM00000.pac",std::string(32,'s'));
    Write(output/L"game/model/test.bin","loose-test-bytes");Write(output/L"system/test.bin","system");
    loose::Manifest manifest;
    for(auto name:{"game00000.pac","game00001.pac","system00000.pac"}){
        auto s=loose::Identify(contents/name,20);s.name=name;s.group=pac::ArchiveNamespace(name);manifest.sources.push_back(s);
    }
    manifest.files.push_back({0,"model/test.bin",16,loose::HashFile(output/L"game/model/test.bin")});
    manifest.files.push_back({2,"test.bin",6,loose::HashFile(output/L"system/test.bin")});
    loose::WriteManifest(output/L"manifest.vii",manifest);
    auto state=LoadLooseState(output,contents,true);CHECK(state->files.size()==2);
    state->allocate=Allocate;state->release=Release;state->append=reinterpret_cast<AppendFn>(Append);
    alignas(8) char manager[0x50]{};auto lock=reinterpret_cast<CRITICAL_SECTION*>(manager+0x38);InitializeCriticalSection(lock);
    Field<void**>(manager,0xc)=slots;Field<void**>(manager,0x10)=slots;Field<const char*>(manager,0x30)="CONTENTS/GAME";
    uint32_t index=99;
    failAppend=true;bool threw=false;try{OpenLoose(*state,manager,"/MODEL/test.bin",&index);}catch(const std::exception& e){std::printf("expected injected failure: %s\n",e.what());threw=true;}
    std::printf("allocation-failure probe: threw=%d alloc=%u release=%u index=%u mismatch=%llu keys=%s\n",threw,allocations,releases,index,mismatches.load(),state->files.begin()->first.c_str());
    CHECK(threw&&allocations==1&&releases==1&&Field<void**>(manager,0x10)==slots&&index==99);failAppend=false;
    CHECK(OpenLoose(*state,manager,"/MODEL/test.bin",&index)&&index==0&&allocations==2);
    auto entry=slots[0];CHECK(Field<uint32_t>(entry,0x120)==16&&Field<uint32_t>(entry,0x124)==0&&Field<uint32_t>(entry,0x130)==0&&Field<int32_t>(entry,0x13c)==-1);
    char data[17]{};DWORD got=0;CHECK(ReadFile(Field<HANDLE>(entry,4),data,17,&got,nullptr)&&got==16&&std::string(data,got)=="loose-test-bytes");
    CHECK(ReadFile(Field<HANDLE>(entry,4),data,17,&got,nullptr)&&got==0);
    // Live game-owned handle denies replacement/writes until original close.
    HANDLE denied=CreateFileW((output/L"game/model/test.bin").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);CHECK(denied==INVALID_HANDLE_VALUE);
    CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    CHECK(OpenLoose(*state,manager,"/MODEL/test.bin",&index)&&index==0&&allocations==2);
    CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    Write(output/L"game/model/test.bin","Loose-test-bytes");
    CHECK(!OpenLoose(*state,manager,"/MODEL/test.bin",&index)&&mismatches==1);
    state->verify=false;CHECK(OpenLoose(*state,manager,"/MODEL/test.bin",&index));CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    Write(output/L"game/model/test.bin","short");CHECK(!OpenLoose(*state,manager,"/MODEL/test.bin",&index)&&mismatches==2);
    // The opened leaf remains authoritative when its attributes are no longer
    // queried by path. A directory masquerading as a manifest file is refused.
    fs::remove(output/L"game/model/test.bin");fs::create_directory(output/L"game/model/test.bin");
    CHECK(!OpenLoose(*state,manager,"/MODEL/test.bin",&index));
    fs::remove(output/L"game/model/test.bin");
    Junction(output/L"game/model/test.bin",output/L"system");
    CHECK(!OpenLoose(*state,manager,"/MODEL/test.bin",&index));
    CHECK(RemoveDirectoryW((output/L"game/model/test.bin").c_str()));
    Write(output/L"game/model/test.bin","loose-test-bytes");
    fs::rename(output/L"game/model",output/L"game/model-real");
    Junction(output/L"game/model",output/L"game/model-real");
    threw=false;try{OpenLoose(*state,manager,"/MODEL/test.bin",&index);}catch(...){threw=true;}CHECK(threw);
    CHECK(RemoveDirectoryW((output/L"game/model").c_str()));
    fs::rename(output/L"game/model-real",output/L"game/model");
    CHECK(OpenLoose(*state,manager,"/MODEL/test.bin",&index));
    CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    CHECK(!OpenLoose(*state,manager,"/MODEL/missing.bin",&index));
    looseState=std::move(state);originalOpen=reinterpret_cast<OpenFn>(Fallback);looseEnabled=true;
    CHECK(NativeLooseOpen(manager,nullptr,"../unsafe",&index)==1&&index==77&&fallbacks==1&&errors==1);
    CHECK(NativeLooseOpen(manager,nullptr,"/MODEL/missing.bin",&index)==1&&fallbacks==2&&misses==2);
    looseEnabled=false;CHECK(NativeLooseOpen(manager,nullptr,"/MODEL/test.bin",&index)==1&&fallbacks==3&&opens==2);
    LooseStats stats{};CHECK(VIIGetLooseFileStats(&stats,sizeof(stats))&&stats.version==1&&stats.enabled==0&&stats.opens==2);
    CHECK(!VIIGetLooseFileStats(&stats,4));
    // A changed part rejects every file from its group, including unchanged parts.
    Write(contents/L"GAME00001.pac",std::string(32,'x'));
    auto reduced=LoadLooseState(output,contents,true);CHECK(reduced->files.size()==1&&reduced->files.count("contents/system/test.bin")&&sourceRejects==1);
    const auto dlc=root/L"DLC",dlcOutput=loose::DlcOutput(output);
    fs::create_directories(dlc);fs::create_directories(dlcOutput/L"system");
    Write(dlc/L"SYSTEM00000.pac",std::string(32,'d'));Write(dlcOutput/L"system/test.bin","DLC-data");
    loose::Manifest dlcManifest;auto dlcSource=loose::Identify(dlc/L"SYSTEM00000.pac",20);
    dlcSource.name="system00000.pac";dlcSource.group="system";dlcManifest.sources.push_back(dlcSource);
    dlcManifest.files.push_back({0,"test.bin",8,loose::HashFile(dlcOutput/L"system/test.bin")});
    loose::WriteManifest(dlcOutput/L"manifest.vii",dlcManifest);
    auto combined=LoadLooseState(output,contents,true);
    CHECK(combined->files.size()==2&&combined->files.count("contents/system/test.bin")&&combined->files.count("dlc/system/test.bin"));
    combined->allocate=Allocate;combined->release=Release;combined->append=reinterpret_cast<AppendFn>(Append);
    Field<const char*>(manager,0x30)="DLC/SYSTEM";CHECK(OpenLoose(*combined,manager,"test.bin",&index));
    CHECK(ReadFile(Field<HANDLE>(entry,4),data,17,&got,nullptr)&&std::string(data,got)=="DLC-data");
    CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    Field<const char*>(manager,0x30)="CONTENTS/SYSTEM";CHECK(OpenLoose(*combined,manager,"test.bin",&index));
    CHECK(ReadFile(Field<HANDLE>(entry,4),data,17,&got,nullptr)&&std::string(data,got)=="system");
    CloseHandle(Field<HANDLE>(entry,4));Field<unsigned char>(entry,8)=0;
    fs::rename(dlc,root/L"DLC-disabled");auto absent=LoadLooseState(output,contents,false);
    CHECK(absent->files.size()==1&&!absent->files.count("dlc/system/test.bin"));
    fs::rename(root/L"DLC-disabled",dlc);
    Write(dlc/L"SYSTEM00000.pac",std::string(32,'e'));auto stale=LoadLooseState(output,contents,false);
    CHECK(stale->files.size()==1&&!stale->files.count("dlc/system/test.bin"));
    Write(dlcOutput/L"manifest.vii","corrupt");auto damaged=LoadLooseState(output,contents,false);
    CHECK(damaged->files.size()==1&&damaged->files.count("contents/system/test.bin"));
    fs::remove(output/L"manifest.vii");threw=false;try{LoadLooseState(output,contents,false);}catch(...){threw=true;}CHECK(threw);
    Release(entry);DeleteCriticalSection(lock);
    std::printf("PASS %u native loose loader checks: identity groups, handle ownership, hash/size mismatch, slot reuse, allocation cleanup, fallback and switch\n",checks);
}
