#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <vector>
#include <cstddef>
namespace vii {
// Non-owning immutable bytes. The caller retains vector/blob ownership.
struct TextureBytes {
    const unsigned char* pointer; size_t length;
    TextureBytes(const void* p,size_t n):pointer(static_cast<const unsigned char*>(p)),length(n){}
    TextureBytes(const std::vector<unsigned char>& v):pointer(v.data()),length(v.size()){}
    const unsigned char* data() const {return pointer;}
    size_t size() const {return length;}
};
struct MipSlice {size_t offset; UINT rowPitch; UINT slicePitch;};
struct TextureUpload {D3D11_TEXTURE2D_DESC desc{};std::vector<MipSlice> slices;};
bool PrepareTextureUpload(const D3D11_TEXTURE2D_DESC&,TextureBytes,TextureUpload&);
HRESULT UploadTexture(ID3D11Device*,const TextureUpload&,TextureBytes,ID3D11Resource**);
// Returns a lower bound for a DDS serialization of a supported BC texture.
// A caller may reject retention when this alone cannot fit, without readback.
bool MinimumSerializedDdsSize(const D3D11_TEXTURE2D_DESC&,size_t&);
}
