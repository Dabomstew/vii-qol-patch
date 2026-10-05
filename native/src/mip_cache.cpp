#include "patch.hpp"
#include "texture_upload.hpp"
#include "texture_disk.hpp"
#include "texture_pipeline.hpp"
#include "texture_writer.hpp"
#include "game_path.hpp"
#include <wrl/client.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace vii {
struct Entry {
    unsigned recipe=0;std::vector<unsigned char> source,diskDds;ComPtr<ID3DBlob> dds;TextureUpload upload;
    TextureBytes Bytes()const{return dds?TextureBytes(dds->GetBufferPointer(),dds->GetBufferSize()):TextureBytes(diskDds);}
    size_t Cost()const{return source.size()+Bytes().size();}
};
struct State {
    Context context;HMODULE library=nullptr;TextureCreateFn original=nullptr;TextureSaveFn save=nullptr;
    std::list<Entry> entries;ComPtr<ID3D11Device> device;std::atomic<DWORD> owner{0};
    size_t budget=16*1024*1024,bytes=0,maxEntries=128;bool verify=false,traceLarge=false,singleMipCache=false;std::atomic<bool> enabled{true};
    uint64_t hits=0,misses=0,evictions=0,verified=0,mismatches=0,readbackFailures=0,diskHits=0,diskStores=0;
    TextureDisk disk[2];int diskMode=0;bool diskStarted=false;std::wstring diskDirectory,captureDirectory;
    HANDLE trace=INVALID_HANDLE_VALUE;std::mutex traceMutex;std::atomic<uint64_t> sequence{0};
    std::mutex diskWriteMutex;std::unique_ptr<TextureWriter> writer;
};
static State* state=nullptr; // Pinned process lifetime; no loader-lock COM teardown.
static uint32_t Word(const void* p,size_t at){uint32_t n;memcpy(&n,static_cast<const unsigned char*>(p)+at,4);return n;}
static void Trace(State& s,const char* text)noexcept{
    if(s.trace==INVALID_HANDLE_VALUE)return;
    try{std::lock_guard<std::mutex> lock(s.traceMutex);DWORD done=0;size_t n=strlen(text);
        if(!WriteFile(s.trace,text,DWORD(n),&done,nullptr)||done!=n)Log("MipCache trace write failed error=%lu",GetLastError());}catch(...){}
}
struct Request {
    State& s;uint64_t id;LARGE_INTEGER start{};HRESULT result=E_FAIL;const char* event="exception";const char* reason="none";
    bool store=false,queued=false,verified=false;D3D11_TEXTURE2D_DESC desc{};size_t generated=0;
    Request(State& cache,uintptr_t caller,const void* source,size_t size,TextureLoadInfo* options):s(cache),id(++s.sequence){
        QueryPerformanceCounter(&start);
        if(s.trace==INVALID_HANDLE_VALUE)return;
        auto digest=source&&size&&size<=TextureMaxBytes?TextureDigestText(Hash(source,size)):"unavailable";
        if(!s.captureDirectory.empty()&&source&&size&&size<=TextureMaxBytes){
            std::wstring name(digest.begin(),digest.end());auto target=s.captureDirectory+L"\\"+name+L".bin";
            if(GetFileAttributesW(target.c_str())==INVALID_FILE_ATTRIBUTES){
                auto temp=target+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(id)+L".part";
                HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(f!=INVALID_HANDLE_VALUE){DWORD done=0;bool ok=WriteFile(f,source,DWORD(size),&done,nullptr)&&done==size;CloseHandle(f);
                    if(!ok||!MoveFileExW(temp.c_str(),target.c_str(),0))DeleteFileW(temp.c_str());}
            }
        }
        char line[1600];char opts[256]{};if(options)for(unsigned i=0;i<13;++i)sprintf_s(opts+i*9,sizeof(opts)-i*9,"%08x,",options->fields[i]);
        sprintf_s(line,"begin id=%llu qpc=%lld thread=%lu caller_rva=%08x source_sha256=%s source_bytes=%zu options=%s\n",id,start.QuadPart,GetCurrentThreadId(),unsigned(caller),digest.c_str(),size,options?opts:"null");Trace(s,line);
    }
    ~Request(){LARGE_INTEGER end{};QueryPerformanceCounter(&end);char line[768];sprintf_s(line,
        "end id=%llu qpc=%lld ticks=%lld event=%s reason=%s hr=%08lx stored=%u queued=%u verified=%u generated_bytes=%zu width=%u height=%u mips=%u format=%u\n",
        id,end.QuadPart,end.QuadPart-start.QuadPart,event,reason,result,store,queued,verified,generated,desc.Width,desc.Height,desc.MipLevels,desc.Format);Trace(s,line);}
};
static ComPtr<ID3DBlob> Serialize(State& s,ID3D11Resource* resource){
    ComPtr<ID3D11DeviceContext> context;s.device->GetImmediateContext(&context);ComPtr<ID3DBlob> blob;
    if(!context||FAILED(s.save(context.Get(),resource,4,&blob,0))||!blob||blob->GetBufferSize()>TextureMaxBytes)return {};return blob;
}
static void Retain(State& s,Entry&& entry){
    const auto cost=entry.Cost();if(cost>s.budget)return;
    while(s.bytes>s.budget-cost||s.entries.size()>=s.maxEntries){s.bytes-=s.entries.front().Cost();s.entries.pop_front();++s.evictions;}
    s.entries.push_back(std::move(entry));s.bytes+=cost;
}
__declspec(noinline) static HRESULT WINAPI Create(ID3D11Device* device,const void* source,SIZE_T size,TextureLoadInfo* options,void* pump,ID3D11Resource** output,HRESULT* asyncResult){
    State& s=*state;auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-reinterpret_cast<uintptr_t>(s.context.game);
    HRESULT result=E_FAIL;bool originalCalled=false,cachedOutput=false;
    auto original=[&]{originalCalled=true;result=s.original(device,source,size,options,pump,output,asyncResult);return result;};
    // This caller requests the original single-mip recipe. Opting out forwards
    // every argument unchanged before diagnostic hashing or cache processing.
    if(!s.singleMipCache&&caller==0x38b3ce)return original();
    try{
        Request request(s,caller,source,size,options);TextureLoadInfo defaults;unsigned recipe=caller==0x38b3ce?1:0;if(recipe)defaults.fields[4]=1;
        const char* bypass=!s.enabled?"disabled":caller!=0x38ba10&&caller!=0x38b3ce?"caller":pump||asyncResult?"asynchronous":!source||size<128?"source":size>TextureMaxBytes?"source_limit":
            !options||memcmp(options,&defaults,sizeof(defaults))?"options":!output||!device?"output_device":Word(source,0)!=0x20534444?"not_dds":
            (Word(source,84)!=0x31545844&&Word(source,84)!=0x33545844&&Word(source,84)!=0x35545844&&!(Word(source,84)==0&&Word(source,88)==32))?"format":nullptr;
        if(bypass){request.event="bypass";request.reason=bypass;request.result=original();return result;}
        DWORD current=GetCurrentThreadId(),none=0;if(s.owner.compare_exchange_strong(none,current))s.device=device;
        if(s.owner.load()!=current||s.device.Get()!=device){request.event="bypass";request.reason="thread_device";request.result=original();return result;}
        if(s.diskMode&&!s.diskStarted){
            s.diskStarted=true;
            try{auto identity=TexturePipelineIdentity(device,s.library);if(!s.disk[0].Open(s.diskDirectory,identity)||!s.disk[1].Open(s.diskDirectory,TexturePipelineIdentity(device,s.library,true)))s.diskMode=0;
                Log("MipCache disk mode=%d identity=%s initialized",s.diskMode,TextureDigestText(identity).c_str());}
            catch(const std::exception& e){s.diskMode=0;Log("MipCache disk disabled: %s",e.what());}
            if(s.diskMode==2)try{s.writer=std::make_unique<TextureWriter>(32u*1024*1024,[&s](const TextureWriteRecord& r){
                bool ok=false;try{std::lock_guard<std::mutex> lock(s.diskWriteMutex);ok=s.disk[r.recipe].Write(r.source,r.dds,r.desc);}catch(...){}
                char line[128];sprintf_s(line,"store id=%llu ok=%u\n",r.request,ok);Trace(s,line);if(!ok)Log("MipCache asynchronous store failed request=%llu",r.request);
            });}catch(...){Log("MipCache writer unavailable; using synchronous stores");}
        }
        Entry entry;entry.recipe=recipe;bool hit=false;
        for(auto it=s.entries.begin();it!=s.entries.end();++it)if(it->recipe==recipe&&it->source.size()==size&&!memcmp(it->source.data(),source,size)){
            s.bytes-=it->Cost();entry=std::move(*it);s.entries.erase(it);hit=true;request.event="ram_hit";break;}
        if(!hit&&s.diskMode){
            try{hit=s.disk[recipe].Read({source,size},entry.diskDds,entry.upload);}catch(const std::exception& e){Log("MipCache disk read failed: %s",e.what());}
            if(hit){++s.diskHits;request.event="disk_hit";Log("MipCache disk_hit count=%llu source_bytes=%zu",s.diskHits,size);}
        }
        if(hit){
            ++s.hits;request.desc=entry.upload.desc;request.generated=entry.Bytes().size();result=UploadTexture(device,entry.upload,entry.Bytes(),output);cachedOutput=SUCCEEDED(result)&&*output;
            if(SUCCEEDED(result)&&*output&&s.verify){
                auto actual=Serialize(s,*output);
                if(!actual){++s.readbackFailures;request.reason="readback_failed";result=E_FAIL;}
                else if(actual->GetBufferSize()!=entry.Bytes().size()||memcmp(actual->GetBufferPointer(),entry.Bytes().data(),actual->GetBufferSize())){
                    ++s.mismatches;s.enabled=false;request.reason="verification_mismatch";result=E_FAIL;Log("MipCache verification mismatch; disabling cache");
                }else{++s.verified;request.verified=true;}
            }
            if(SUCCEEDED(result)&&*output){
                request.result=result;
                if(entry.Cost()<=s.budget&&size<=s.budget-entry.Bytes().size()){
                    if(entry.source.empty())entry.source.assign(static_cast<const unsigned char*>(source),static_cast<const unsigned char*>(source)+size);
                    Retain(s,std::move(entry));}
                return result;
            }
            if(*output){(*output)->Release();*output=nullptr;}cachedOutput=false;request.event="hit_failed_generate";
            if(!strcmp(request.reason,"none"))request.reason="upload_failed";
        }else{++s.misses;request.event="generate";request.reason=s.diskMode?"disk_absent_or_invalid":"disk_disabled";}
        request.result=original();
        if(SUCCEEDED(result)&&*output&&s.enabled){
            ComPtr<ID3D11Texture2D> typed;if(SUCCEEDED((*output)->QueryInterface(IID_PPV_ARGS(&typed))))typed->GetDesc(&request.desc);
            size_t minimum=0;
            if(typed&&MinimumSerializedDdsSize(request.desc,minimum)&&minimum<=TextureMaxBytes&&
                (s.diskMode==2||(size<=s.budget&&minimum<=s.budget-size))){
                entry=Entry{};entry.recipe=recipe;entry.dds=Serialize(s,*output);request.generated=entry.Bytes().size();
                if(entry.dds&&PrepareTextureUpload(request.desc,entry.Bytes(),entry.upload)){
                    if(s.diskMode==2){try{
                        if(s.writer)request.queued=s.writer->Enqueue(request.id,recipe,{source,size},entry.Bytes(),request.desc);
                        if(!request.queued){std::lock_guard<std::mutex> lock(s.diskWriteMutex);request.store=s.disk[recipe].Write({source,size},entry.Bytes(),request.desc);}
                    }catch(const std::exception& e){Log("MipCache disk store failed: %s",e.what());}
                        if(request.store)++s.diskStores;else if(!request.queued)Log("MipCache disk store failed request=%llu",request.id);}
                    if(size<=s.budget&&entry.Bytes().size()<=s.budget-size){entry.source.assign(static_cast<const unsigned char*>(source),static_cast<const unsigned char*>(source)+size);Retain(s,std::move(entry));}
                }else{request.reason="serialization_or_layout_failed";++s.readbackFailures;}
            }else request.reason="unsupported_layout_or_resource_limit";
        }
        if((s.hits+s.misses)%100==0||s.hits+s.misses==1)Log("MipCache hits=%llu misses=%llu entries=%zu bytes=%zu evictions=%llu verified=%llu mismatches=%llu readback_failures=%llu disk_hits=%llu disk_stores=%llu",s.hits,s.misses,s.entries.size(),s.bytes,s.evictions,s.verified,s.mismatches,s.readbackFailures,s.diskHits,s.diskStores);
        return result;
    }catch(const std::exception& e){Log("MipCache exception: %s",e.what());if(originalCalled)return result;if(cachedOutput&&output&&*output){(*output)->Release();*output=nullptr;}return original();}
}
bool InstallMipCache(const Context& context){
    auto library=GetModuleHandleW(L"d3dx11_43.dll");if(!library)return false;
    auto candidate=std::make_unique<State>();candidate->context=context;candidate->library=library;
    candidate->original=reinterpret_cast<TextureCreateFn>(GetProcAddress(library,"D3DX11CreateTextureFromMemory"));
    candidate->save=reinterpret_cast<TextureSaveFn>(GetProcAddress(library,"D3DX11SaveTextureToMemory"));if(!candidate->original||!candidate->save)return false;
    int mib=Option(context,L"MipCache",L"BudgetMiB",16),entries=Option(context,L"MipCache",L"MaxEntries",128);
    if(mib<1||mib>256||entries<1||entries>1024)return false;
    candidate->budget=size_t(mib)*1024*1024;candidate->maxEntries=entries;candidate->verify=Option(context,L"MipCache",L"Verify",0)!=0;
    candidate->traceLarge=Option(context,L"MipCache",L"TraceLarge",0)!=0;
    candidate->singleMipCache=Option(context,L"MipCache",L"SingleMipCache",0)!=0;
    candidate->diskMode=Option(context,L"MipCache",L"DiskMode",0);if(candidate->diskMode<0||candidate->diskMode>2)return false;
    wchar_t directory[32768]{};GetPrivateProfileStringW(L"MipCache",L"DiskDirectory",L"vii-speedrun-patch\\cache",directory,32768,context.ini.c_str());
    candidate->diskDirectory=ResolveGamePath(context.directory,directory,L"vii-speedrun-patch\\cache").wstring();
    if(Option(context,L"MipCache",L"Trace",0)){
        auto folder=context.directory+L"\\vii-texture-logs";CreateDirectoryW(folder.c_str(),nullptr);
        if(Option(context,L"MipCache",L"CaptureSources",0)){candidate->captureDirectory=folder+L"\\sources";CreateDirectoryW(candidate->captureDirectory.c_str(),nullptr);}
        SYSTEMTIME now{};GetSystemTime(&now);wchar_t name[160];
        swprintf_s(name,L"\\textures-%04u%02u%02uT%02u%02u%02u-%lu.log",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
        candidate->trace=CreateFileW((folder+name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(candidate->trace==INVALID_HANDLE_VALUE){Log("MipCache cannot create trace error=%lu",GetLastError());return false;}
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);char line[160];sprintf_s(line,"session version=1 pid=%lu qpc_frequency=%lld\n",GetCurrentProcessId(),frequency.QuadPart);Trace(*candidate,line);
    }
    state=candidate.get();if(!ReplaceImport(context,0x4ed4b8,reinterpret_cast<void*>(candidate->original),reinterpret_cast<void*>(Create))){if(candidate->trace!=INVALID_HANDLE_VALUE)CloseHandle(candidate->trace);state=nullptr;return false;}
    Log("MipCache configuration budget_mib=%d verify=%d max_entries=%d disk_mode=%d full_trace=%d single_mip_cache=%d",mib,candidate->verify,entries,candidate->diskMode,candidate->trace!=INVALID_HANDLE_VALUE,candidate->singleMipCache);
    candidate.release();return true;
}
}
