#include "texture_prepare.hpp"
#include "texture_sources.hpp"
#include "texture_pipeline.hpp"
#include "texture_disk.hpp"
#include <wrl/client.h>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <stdexcept>
#pragma comment(lib,"d3d11.lib")
namespace vii::prepare {
namespace fs=std::filesystem;
using Microsoft::WRL::ComPtr;
namespace {
struct Mapping {
    HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;const void* data=nullptr;size_t size=0;
    explicit Mapping(const fs::path& p){
        file=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        LARGE_INTEGER n{};
        if(file!=INVALID_HANDLE_VALUE&&GetFileSizeEx(file,&n)&&n.QuadPart>0&&uint64_t(n.QuadPart)<=TextureMaxBytes){
            size=size_t(n.QuadPart);mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);if(mapping)data=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);}
    }
    ~Mapping(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
};
std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,' ');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
void Check(const pac::Cancel& cancel){if(cancel&&cancel())throw std::runtime_error("Cancelled; completed textures are preserved");}
std::wstring Stamp(){SYSTEMTIME t{};GetSystemTime(&t);wchar_t s[120];swprintf_s(s,L"prepare-%04u%02u%02uT%02u%02u%02u-%lu.tsv",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());return s;}
}
Result Build(const fs::path& assets,const fs::path& cache,const Report& report,const pac::Cancel& cancel,const fs::path& d3dxPath,const fs::path& dlcAssets){
    if(!fs::is_directory(assets))throw std::runtime_error("Unpacked asset directory is missing");
    Result result;result.output=fs::absolute(cache).lexically_normal();
    const auto cacheRelative=fs::weakly_canonical(result.output).lexically_relative(fs::weakly_canonical(assets));
    if(!cacheRelative.empty()&&*cacheRelative.begin()!=L"..")throw std::runtime_error("Choose a cache folder outside the unpacked asset folder");
    if(!dlcAssets.empty()){
        const auto relative=fs::weakly_canonical(result.output).lexically_relative(fs::weakly_canonical(dlcAssets));
        if(!relative.empty()&&*relative.begin()!=L"..")throw std::runtime_error("Choose a cache folder outside the unpacked DLC folder");
    }
    fs::create_directories(result.output);result.log=result.output/Stamp();
    std::ofstream log(result.log,std::ios::binary);if(!log)throw std::runtime_error("Cannot create preparation report");
    auto record=[&](const char* event,const fs::path& file,size_t offset,unsigned image,const std::string& hash,const std::string& detail){
        log<<event<<'\t'<<Utf8(file.wstring())<<'\t'<<offset<<'\t'<<image<<'\t'<<hash<<'\t'<<detail<<'\n';log.flush();if(!log)throw std::runtime_error("Preparation report write failed");};
    HMODULE library=LoadLibraryW(d3dxPath.empty()?L"d3dx11_43.dll":d3dxPath.c_str());if(!library)throw std::runtime_error("The installed x86 D3DX11_43 runtime is unavailable");
    auto create=reinterpret_cast<TextureCreateFn>(GetProcAddress(library,"D3DX11CreateTextureFromMemory"));
    auto save=reinterpret_cast<TextureSaveFn>(GetProcAddress(library,"D3DX11SaveTextureToMemory"));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(hr)||!create||!save)throw std::runtime_error("Cannot create the game's texture-generation device");
    auto identity=TexturePipelineIdentity(device.Get(),library);TextureDisk disk[2];
    if(!disk[0].Open(result.output.wstring(),identity)||!disk[1].Open(result.output.wstring(),TexturePipelineIdentity(device.Get(),library,true)))throw std::runtime_error("Cannot open texture cache; close the game or other preparer");
    record("identity",{},0,0,TextureDigestText(identity),"x86-default-mip-recipe");
    auto serialize=[&](ID3D11Resource* resource){ComPtr<ID3DBlob> blob;
        if(FAILED(save(context.Get(),resource,4,&blob,0))||!blob||blob->GetBufferSize()>TextureMaxBytes)throw std::runtime_error("Texture readback failed or exceeded resource bound");return blob;};
    std::vector<std::pair<fs::path,fs::path>> files;Progress progress;progress.current=L"Finding texture sources...";if(report)report(progress);
    auto collect=[&](const fs::path& root,const fs::path& prefix){
        for(fs::recursive_directory_iterator it(root),end;it!=end;++it){Check(cancel);
            if(it->is_symlink()){if(it->is_directory())it.disable_recursion_pending();continue;}
            if(it->is_regular_file()){files.emplace_back(it->path(),prefix/fs::relative(it->path(),root));progress.totalBytes+=it->file_size();}}
    };
    collect(assets,{});if(!dlcAssets.empty())collect(dlcAssets,L"DLC");
    std::sort(files.begin(),files.end());progress.totalFiles=files.size();std::unordered_set<std::string> seen;
    for(const auto& item:files){const auto& file=item.first;
        Check(cancel);progress.current=file.wstring();if(report)report(progress);Mapping mapped(file);
        const auto& relative=item.second;
        if(!mapped.data){if(fs::file_size(file)){++result.failed;record("file_unreadable",relative,0,0,"","mapping/resource limit");}}
        else ScanTextureSources({mapped.data,mapped.size},[&](TextureSource&& source){
            Check(cancel);
            if(!source.issue.empty()){++result.unsupported;record("unsupported",relative,source.offset,source.image,"",source.issue);return;}
            if(source.dds.empty())return;
            auto hash=TextureDigestText(Hash(source.dds.data(),source.dds.size()));
            if(!seen.insert(std::to_string(source.singleMip)+hash).second){++result.reused;record("duplicate",relative,source.offset,source.image,hash,"");return;}
            record("begin",relative,source.offset,source.image,hash,"");
            progress.current=L"Preparing "+relative.wstring()+L" ("+std::to_wstring(result.generated)+L" generated, "+std::to_wstring(result.reused)+L" reused)";if(report)report(progress);
            try{
                std::vector<unsigned char> existing;TextureUpload upload;
                if(disk[source.singleMip].Read(source.dds,existing,upload)){++result.reused;record("reused",relative,source.offset,source.image,hash,"");return;}
                TextureLoadInfo options;if(source.singleMip)options.fields[4]=1;ComPtr<ID3D11Resource> original,replay;
                HRESULT created=create(device.Get(),source.dds.data(),source.dds.size(),&options,nullptr,&original,nullptr);
                if(FAILED(created)||!original)throw std::runtime_error("D3DX generation failed");
                ComPtr<ID3D11Texture2D> typed;if(FAILED(original.As(&typed)))throw std::runtime_error("Generated resource is not Texture2D");
                D3D11_TEXTURE2D_DESC desc{};typed->GetDesc(&desc);auto generated=serialize(original.Get());
                TextureBytes bytes(generated->GetBufferPointer(),generated->GetBufferSize());
                if(!PrepareTextureUpload(desc,bytes,upload)||FAILED(UploadTexture(device.Get(),upload,bytes,&replay))||!replay)throw std::runtime_error("Generated layout cannot be replayed");
                auto actual=serialize(replay.Get());
                if(actual->GetBufferSize()!=bytes.size()||memcmp(actual->GetBufferPointer(),bytes.data(),bytes.size()))throw std::runtime_error("Replay verification mismatch");
                Check(cancel);
                if(!disk[source.singleMip].Write(source.dds,bytes,desc))throw std::runtime_error("Atomic cache write failed");
                ++result.generated;record("generated_verified",relative,source.offset,source.image,hash,std::to_string(bytes.size()));
            }catch(const std::exception& e){Check(cancel);++result.failed;record("failed",relative,source.offset,source.image,hash,e.what());}
        });
        ++progress.files;progress.bytes+=fs::file_size(file);result.files=progress.files;result.bytes=progress.bytes;if(report)report(progress);
    }
    Check(cancel);record("complete",{},0,0,"","generated="+std::to_string(result.generated)+" reused="+std::to_string(result.reused)+" unsupported="+std::to_string(result.unsupported)+" failed="+std::to_string(result.failed));
    return result;
}
}
