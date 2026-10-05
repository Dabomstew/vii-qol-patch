#include "texture_upload.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace vii {
namespace {
uint32_t Read(TextureBytes data,size_t offset){uint32_t value=0;if(offset+4<=data.size())memcpy(&value,data.data()+offset,4);return value;}
struct Format {unsigned block=0,pixel=0;uint32_t fourcc=0,r=0,g=0,b=0,a=0;};
bool GetFormat(DXGI_FORMAT value,Format& f){
    switch(value){
    case DXGI_FORMAT_BC1_TYPELESS:case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:f.block=8;f.fourcc=0x31545844;break;
    case DXGI_FORMAT_BC2_TYPELESS:case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC2_UNORM_SRGB:f.block=16;f.fourcc=0x33545844;break;
    case DXGI_FORMAT_BC3_TYPELESS:case DXGI_FORMAT_BC3_UNORM:case DXGI_FORMAT_BC3_UNORM_SRGB:f.block=16;f.fourcc=0x35545844;break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:f.pixel=4;f.r=0xff;f.g=0xff00;f.b=0xff0000;f.a=0xff000000;break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:f.pixel=4;f.r=0xff0000;f.g=0xff00;f.b=0xff;f.a=0xff000000;break;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:case DXGI_FORMAT_B8G8R8X8_UNORM:case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:f.pixel=4;f.r=0xff0000;f.g=0xff00;f.b=0xff;break;
    default:return false;
    }return true;
}
uint64_t Row(UINT width,const Format& f){return f.block?((uint64_t(width)+3)/4)*f.block:uint64_t(width)*f.pixel;}
uint64_t Rows(UINT height,const Format& f){return f.block?(uint64_t(height)+3)/4:height;}
}
bool MinimumSerializedDdsSize(const D3D11_TEXTURE2D_DESC& desc,size_t& output){
    if(!desc.Width||!desc.Height||desc.Width>16384||desc.Height>16384||!desc.MipLevels||!desc.ArraySize||desc.SampleDesc.Count!=1||desc.MipLevels>32||desc.ArraySize>2048)return false;
    Format format;if(!GetFormat(desc.Format,format))return false;
    uint64_t total=128;
    for(UINT item=0;item<desc.ArraySize;++item){UINT width=desc.Width,height=desc.Height;
        for(UINT mip=0;mip<desc.MipLevels;++mip){uint64_t bytes=Row(width,format)*Rows(height,format);if(bytes>SIZE_MAX-total)return false;
            total+=bytes;width=std::max(1u,width/2);height=std::max(1u,height/2);}}
    output=static_cast<size_t>(total);return true;
}
bool PrepareTextureUpload(const D3D11_TEXTURE2D_DESC& desc,TextureBytes data,TextureUpload& output){
    if(!data.data()||data.size()<128||Read(data,0)!=0x20534444||Read(data,4)!=124||Read(data,76)!=32||
       !desc.Width||!desc.Height||desc.Width>16384||desc.Height>16384||!desc.MipLevels||!desc.ArraySize||desc.SampleDesc.Count!=1||
       desc.Width!=Read(data,16)||desc.Height!=Read(data,12)||desc.MipLevels!=std::max(1u,Read(data,28)))return false;
    Format format;if(!GetFormat(desc.Format,format))return false;
    size_t offset=128;auto fourcc=Read(data,84);bool cube=(desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE)!=0;
    if(fourcc==0x30315844){
        if(data.size()<148||Read(data,128)!=static_cast<uint32_t>(desc.Format)||Read(data,132)!=3||
           ((Read(data,136)&4)!=0)!=cube||uint64_t(Read(data,140))*(cube?6u:1u)!=desc.ArraySize)return false;offset=148;
    }else{
        if(desc.ArraySize!=(cube?6u:1u)||((Read(data,112)&0x200)!=0)!=cube||(Read(data,112)&0x200000))return false;
        if(cube&&(Read(data,112)&0xfc00)!=0xfc00)return false;
        if(format.block){if(fourcc!=format.fourcc)return false;}
        else if(fourcc!=0||Read(data,88)!=32||Read(data,92)!=format.r||Read(data,96)!=format.g||Read(data,100)!=format.b||Read(data,104)!=format.a)return false;
    }
    if(desc.MipLevels>32||desc.ArraySize>2048)return false;
    TextureUpload layout;layout.desc=desc;layout.slices.reserve(size_t(desc.MipLevels)*desc.ArraySize);
    for(UINT item=0;item<desc.ArraySize;++item){UINT width=desc.Width,height=desc.Height;
        for(UINT mip=0;mip<desc.MipLevels;++mip){uint64_t row=Row(width,format),size=row*Rows(height,format);
            if(row>UINT32_MAX||size>UINT32_MAX||offset>data.size()||size>data.size()-offset)return false;
            layout.slices.push_back({offset,UINT(row),UINT(size)});offset+=size_t(size);width=std::max(1u,width/2);height=std::max(1u,height/2);}}
    if(offset!=data.size())return false;output=std::move(layout);return true;
}
HRESULT UploadTexture(ID3D11Device* device,const TextureUpload& layout,TextureBytes data,ID3D11Resource** output){
    if(!device||!output||!data.data()||layout.slices.size()!=size_t(layout.desc.MipLevels)*layout.desc.ArraySize)return E_INVALIDARG;
    *output=nullptr;std::vector<D3D11_SUBRESOURCE_DATA> initial;initial.reserve(layout.slices.size());
    for(const auto& slice:layout.slices){if(slice.offset>data.size()||slice.slicePitch>data.size()-slice.offset)return E_INVALIDARG;
        initial.push_back({data.data()+slice.offset,slice.rowPitch,slice.slicePitch});}
    ID3D11Texture2D* texture=nullptr;auto result=device->CreateTexture2D(&layout.desc,initial.data(),&texture);
    if(SUCCEEDED(result))*output=texture;else if(texture)texture->Release();return result;
}
}
