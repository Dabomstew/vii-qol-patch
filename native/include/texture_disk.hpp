#pragma once
#include "patch.hpp"
#include "texture_upload.hpp"
namespace vii {
// Disk retention is unlimited. Per-record bounds protect the 32-bit reader;
// they are independent of the in-memory LRU. One writer per identity namespace.
constexpr size_t TextureMaxBytes=512u*1024*1024;
class TextureDisk {
    std::wstring directory;
    Digest identity{};
    HANDLE lock=INVALID_HANDLE_VALUE;
public:
    ~TextureDisk(){if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);}
    TextureDisk()=default;
    TextureDisk(const TextureDisk&)=delete;
    TextureDisk& operator=(const TextureDisk&)=delete;
    bool Open(const std::wstring&,const Digest&);
    std::wstring Path(TextureBytes) const;
    // Preparation defaults to full content validation. Runtime Verify=0 skips
    // stored-source reads and the record checksum, retaining size/layout guards.
    bool Read(TextureBytes,std::vector<unsigned char>&,TextureUpload&,bool verifyContents=true);
    bool Write(TextureBytes,TextureBytes,const D3D11_TEXTURE2D_DESC&);
};
}
