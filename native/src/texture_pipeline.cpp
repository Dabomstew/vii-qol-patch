#include "texture_pipeline.hpp"
#include <dxgi.h>
#include <wrl/client.h>
#include <stdexcept>
namespace vii {
std::string TextureDigestText(const Digest& hash){const char* digits="0123456789abcdef";std::string text;for(auto b:hash){text+=digits[b>>4];text+=digits[b&15];}return text;}
Digest TexturePipelineIdentity(ID3D11Device* device,HMODULE d3dx,bool singleMip){
    static_assert(sizeof(void*)==4,"Use the game's x86 D3DX runtime");
    using Microsoft::WRL::ComPtr;ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};LARGE_INTEGER version{};
    if(!device||!d3dx||FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->GetAdapter(&adapter))||
        FAILED(adapter->GetDesc(&desc))||FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version)))throw std::runtime_error("Texture device identity unavailable");
    constexpr char recipe[]="VII-VMT2-default-D3DX11-43-saveDDS-directUpload-v2-x86";
    auto library=HashFile(ModulePath(d3dx));std::vector<unsigned char> key(recipe,recipe+sizeof(recipe));key.insert(key.end(),library.begin(),library.end());
    const uint32_t values[]={desc.VendorId,desc.DeviceId,desc.SubSysId,desc.Revision,version.LowPart,uint32_t(version.HighPart),uint32_t(device->GetFeatureLevel())};
    if(singleMip){constexpr char tag[]="single-mip-v1";key.insert(key.end(),tag,tag+sizeof(tag));}
    auto p=reinterpret_cast<const unsigned char*>(values);key.insert(key.end(),p,p+sizeof(values));return Hash(key.data(),key.size());
}
}
