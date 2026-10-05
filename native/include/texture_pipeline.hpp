#pragma once
#include "patch.hpp"
#include <d3d11.h>
namespace vii {
struct TextureLoadInfo {uint32_t fields[13]={~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,0xfffffffdu,~0u,~0u,0};};
using TextureCreateFn=HRESULT(WINAPI*)(ID3D11Device*,const void*,SIZE_T,TextureLoadInfo*,void*,ID3D11Resource**,HRESULT*);
using TextureSaveFn=HRESULT(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource*,int,ID3DBlob**,UINT);
// Bump the recipe when source/options/upload/serialization semantics change.
// Unrelated proxy changes do not invalidate generated textures. Adapter LUID
// is a session locator, not a generation input; exclude it so reboot is stable.
Digest TexturePipelineIdentity(ID3D11Device*,HMODULE d3dx,bool singleMip=false);
std::string TextureDigestText(const Digest&);
}
