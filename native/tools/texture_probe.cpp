// Replay the actual D3DX create/save/recreate/save seam on a hardware D3D11 device.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "texture_upload.hpp"
using Microsoft::WRL::ComPtr;
using Bytes=std::vector<unsigned char>;
struct LoadInfo {uint32_t fields[13]={~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,0xfffffffdu,~0u,~0u,0};};
using CreateFn=HRESULT(WINAPI*)(ID3D11Device*,const void*,SIZE_T,LoadInfo*,void*,ID3D11Resource**,HRESULT*);
using SaveFn=HRESULT(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource*,int,ID3DBlob**,UINT);
static uint32_t Word(const Bytes& bytes,size_t offset) {uint32_t value=0;if(offset+4<=bytes.size())memcpy(&value,bytes.data()+offset,4);return value;}
static void Write(const std::filesystem::path& path,const Bytes& bytes) {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
static Bytes Synthetic(unsigned width,unsigned height,unsigned levels,bool bc3) {
    uint32_t h[32]={};h[0]=0x20534444;h[1]=124;h[2]=0x81007;h[3]=height;h[4]=width;h[7]=levels;
    h[19]=32;h[20]=4;h[21]=bc3?0x35545844:0x31545844;h[27]=0x1000;
    if(levels>1){h[2]|=0x20000;h[27]|=0x400008;}
    size_t size=0;
    for(unsigned i=0,w=width,t=height;i<levels;++i,w=std::max(1u,w/2),t=std::max(1u,t/2)) {
        auto n=static_cast<size_t>((w+3)/4)*((t+3)/4)*(bc3?16:8);if(!i)h[5]=static_cast<uint32_t>(n);size+=n;
    }
    Bytes bytes(128+size);memcpy(bytes.data(),h,128);
    uint32_t random=width*1009+height*9176+levels;
    for(size_t i=128;i<bytes.size();++i){random=random*1664525+1013904223;bytes[i]=static_cast<unsigned char>(random>>24);}
    return bytes;
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3||argc>4||(argc==4&&wcscmp(argv[3],L"--single-mip"))){puts("texture-probe <DDS corpus folder> <output folder> [--single-mip]");return 2;}
    const bool singleMip=argc==4;
    auto library=LoadLibraryW(L"d3dx11_43.dll");if(!library){puts("Missing existing D3DX11 library");return 3;}
    auto create=reinterpret_cast<CreateFn>(GetProcAddress(library,"D3DX11CreateTextureFromMemory"));
    auto save=reinterpret_cast<SaveFn>(GetProcAddress(library,"D3DX11SaveTextureToMemory"));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(hr)||!create||!save){printf("Device/API unavailable %08lx\n",hr);return 4;}
    std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
    unsigned equal=0,different=0,rejected=0,saveFailed=0,directEqual=0,directFailed=0,boundsChecked=0,minimumChecked=0;
    auto serialize=[&](ID3D11Resource* resource,Bytes& bytes) {
        ComPtr<ID3DBlob> blob;auto result=save(context.Get(),resource,4,&blob,0);
        if(SUCCEEDED(result)&&blob){auto p=static_cast<const unsigned char*>(blob->GetBufferPointer());bytes.assign(p,p+blob->GetBufferSize());}
        return result;
    };
    auto test=[&](const Bytes& source,const std::string& label) {
        LoadInfo options;ComPtr<ID3D11Resource> original,reloaded;
        if(singleMip)options.fields[4]=1;
        auto first=create(device.Get(),source.data(),source.size(),&options,nullptr,&original,nullptr);
        if(FAILED(first)||!original){++rejected;return;}
        Bytes expected,actual;auto saved=serialize(original.Get(),expected);
        if(FAILED(saved)||expected.empty()){++saveFailed;printf("SAVE_FAILED %s hr=%08lx\n",label.c_str(),saved);return;}
        ComPtr<ID3D11Texture2D> typed;D3D11_TEXTURE2D_DESC desc{};vii::TextureUpload layout;
        ComPtr<ID3D11Resource> direct;
        if(FAILED(original.As(&typed))) {++directFailed;return;}
        typed->GetDesc(&desc);
        size_t minimum=0;
        if(!vii::MinimumSerializedDdsSize(desc,minimum) || minimum>expected.size()) {++directFailed;printf("MINIMUM_SIZE_FAILURE %s minimum=%zu actual=%zu\n",label.c_str(),minimum,expected.size());return;}
        ++minimumChecked;
        // Exercise the production ownership path: retain serialization after the
        // source GPU resource is released, move its owner, and upload its view.
        ComPtr<ID3DBlob> retained;
        if(FAILED(save(context.Get(),original.Get(),4,&retained,0)) || !retained) {++saveFailed;return;}
        original.Reset();typed.Reset();
        auto moved=std::move(retained);
        vii::TextureBytes view(moved->GetBufferPointer(),moved->GetBufferSize());
        if(!vii::PrepareTextureUpload(desc,view,layout) || FAILED(vii::UploadTexture(device.Get(),layout,view,&direct))) {++directFailed;printf("DIRECT_FAILED %s\n",label.c_str());return;}
        auto truncated=expected;truncated.pop_back();vii::TextureUpload invalid;
        auto wrong=desc;wrong.Width++;
        if(vii::PrepareTextureUpload(desc,vii::TextureBytes(nullptr,expected.size()),invalid) || vii::PrepareTextureUpload(desc,truncated,invalid) || vii::PrepareTextureUpload(wrong,expected,invalid)) {++directFailed;puts("BOUNDS_FAILURE");return;}
        ++boundsChecked;
        Bytes directBytes;
        if(FAILED(serialize(direct.Get(),directBytes)) || directBytes!=expected) {++directFailed;printf("DIRECT_MISMATCH %s\n",label.c_str());}
        else ++directEqual;
        options=LoadInfo{};
        if(singleMip)options.fields[4]=1;
        auto second=create(device.Get(),expected.data(),expected.size(),&options,nullptr,&reloaded,nullptr);
        if(FAILED(second)||!reloaded){++different;printf("RELOAD_FAILED %s hr=%08lx\n",label.c_str(),second);return;}
        saved=serialize(reloaded.Get(),actual);
        if(FAILED(saved)||actual.empty()){++saveFailed;printf("SECOND_SAVE_FAILED %s hr=%08lx\n",label.c_str(),saved);return;}
        if(expected==actual){++equal;return;}
        ++different;size_t offset=0;while(offset<std::min(expected.size(),actual.size())&&expected[offset]==actual[offset])++offset;
        printf("MISMATCH %s source=%ux%u mips=%u expected=%zu actual=%zu first=%zu saved_mips=%u/%u\n",label.c_str(),Word(source,16),Word(source,12),Word(source,28),expected.size(),actual.size(),offset,Word(expected,28),Word(actual,28));
        fflush(stdout);
        if(different<=12){Write(out/(label+"-source.dds"),source);Write(out/(label+"-expected.dds"),expected);Write(out/(label+"-actual.dds"),actual);}
    };
    for(const auto& entry:std::filesystem::directory_iterator(argv[1])) if(entry.path().extension()==L".dds") {
        std::ifstream f(entry.path(),std::ios::binary|std::ios::ate);auto size=f.tellg();if(size<128||size>32*1024*1024)continue;
        Bytes data(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(data.data()),size);test(data,entry.path().stem().string());
    }
    const unsigned dims[][2]={{1,1},{4,4},{8,8},{16,16},{64,64},{128,128},{256,256},{512,512},{1024,1024},
        {4,256},{256,4},{8,128},{128,8},{32,256},{256,32},{64,128},{128,64},
        {12,20},{20,12},{60,100},{100,60},{124,252},{252,124},{3,7},{17,31},{65,127}};
    for(auto& d:dims)for(unsigned levels:{1u,2u})for(bool bc3:{false,true}) {
        auto label="synthetic-"+std::to_string(d[0])+"x"+std::to_string(d[1])+"-m"+std::to_string(levels)+(bc3?"-bc3":"-bc1");
        test(Synthetic(d[0],d[1],levels,bc3),label);
    }
    for(unsigned width:{1u,4u,17u,128u})for(unsigned height:{1u,8u,31u}) {
        auto bc2=Synthetic(width,height,1,true);uint32_t tag=0x33545844;memcpy(bc2.data()+84,&tag,4);test(bc2,"bc2-"+std::to_string(width)+"x"+std::to_string(height));
        for(bool bgra:{false,true}) {
            Bytes raw(128+size_t(width)*height*4);uint32_t h[32]={};h[0]=0x20534444;h[1]=124;h[2]=0x100f;h[3]=height;h[4]=width;h[5]=width*4;h[7]=1;h[19]=32;h[20]=0x41;h[22]=32;
            h[23]=bgra?0xff0000:0xff;h[24]=0xff00;h[25]=bgra?0xff:0xff0000;h[26]=0xff000000;h[27]=0x1000;memcpy(raw.data(),h,128);
            for(size_t i=128;i<raw.size();++i)raw[i]=static_cast<unsigned char>(i*17);test(raw,"raw32-"+std::to_string(width)+"x"+std::to_string(height)+(bgra?"-bgra":"-rgba"));
        }
    }
    // Real array/cubemap resources force serialized DDS DX10/face-layout paths.
    for(UINT count:{2u,6u})for(bool bc3:{false,true}) {
        auto bytes=Synthetic(16,16,3,bc3);D3D11_TEXTURE2D_DESC desc{};
        desc.Width=16;desc.Height=16;desc.MipLevels=3;desc.ArraySize=count;desc.Format=bc3?DXGI_FORMAT_BC3_UNORM:DXGI_FORMAT_BC1_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(count==6)desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        std::vector<D3D11_SUBRESOURCE_DATA> sub;
        for(UINT item=0;item<count;++item) {size_t offset=128;for(UINT dim=16;dim>=4;dim/=2) {UINT row=((dim+3)/4)*(bc3?16:8),size=row*((dim+3)/4);sub.push_back({bytes.data()+offset,row,size});offset+=size;}}
        ComPtr<ID3D11Texture2D> texture;
        if(FAILED(device->CreateTexture2D(&desc,sub.data(),&texture))) {++directFailed;puts("ARRAY_SETUP_FAILED");continue;}
        Bytes saved;if(FAILED(serialize(texture.Get(),saved))) {++saveFailed;continue;}
        test(saved,"array-"+std::to_string(count)+(bc3?"-bc3":"-bc1"));
    }
    printf("SUMMARY legacy_equal=%u legacy_different=%u rejected=%u save_failed=%u direct_equal=%u direct_failed=%u bounds_checked=%u minimum_checked=%u\n",equal,different,rejected,saveFailed,directEqual,directFailed,boundsChecked,minimumChecked);
    return directFailed||saveFailed||!directEqual?1:0;
}
