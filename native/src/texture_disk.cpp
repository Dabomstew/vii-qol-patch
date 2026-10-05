#include "texture_disk.hpp"
#include <bcrypt.h>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
namespace vii {
namespace {
constexpr uint32_t magic=0x32544d56; // VMT2: same layout, isolated recipe namespace
struct Header {uint32_t magic,sourceSize,ddsSize;Digest identity;D3D11_TEXTURE2D_DESC desc;};
static_assert(sizeof(Header)==88,"disk schema changed");
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;void Close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}~Handle(){Close();}};
struct Sha {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    Sha(){if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider");
        if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("SHA256 create");}}
    ~Sha(){BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);}
    void Add(const void* p,size_t n){if(n>MAXDWORD||BCryptHashData(hash,(PUCHAR)p,ULONG(n),0)<0)throw std::runtime_error("SHA256 data");}
    Digest Finish(){Digest d{};if(BCryptFinishHash(hash,d.data(),32,0)<0)throw std::runtime_error("SHA256 finish");return d;}
};
std::wstring Hex(const Digest& hash) {const wchar_t* digits=L"0123456789abcdef";std::wstring out;for(auto b:hash){out+=digits[b>>4];out+=digits[b&15];}return out;}
bool ReadAll(HANDLE f,void* p,size_t n){DWORD done=0;return n<=MAXDWORD&&ReadFile(f,p,DWORD(n),&done,nullptr)&&done==n;}
bool WriteAll(HANDLE f,const void* p,size_t n){DWORD done=0;return n<=MAXDWORD&&WriteFile(f,p,DWORD(n),&done,nullptr)&&done==n;}
}
std::wstring TextureDisk::Path(TextureBytes source) const {return directory+L"\\"+Hex(Hash(source.data(),source.size()))+L".vmt";}
bool TextureDisk::Open(const std::wstring& path,const Digest& key) {
    directory.clear();if(lock!=INVALID_HANDLE_VALUE){CloseHandle(lock);lock=INVALID_HANDLE_VALUE;}
    if(path.size()<3||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return false;
    const auto ns=path+L"\\"+Hex(key);std::error_code error;
    std::filesystem::create_directories(ns,error);if(error)return false;
    lock=CreateFileW((ns+L"\\cache.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock==INVALID_HANDLE_VALUE)return false;
    directory=ns;identity=key;return true; // No startup enumeration, preload, or retention cap.
}
bool TextureDisk::Read(TextureBytes source,std::vector<unsigned char>& generated,TextureUpload& upload) {
    if(directory.empty()||!source.data()||source.size()>TextureMaxBytes)return false;
    Handle file{CreateFileW(Path(source).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER length{};Header h{};
    if(!GetFileSizeEx(file.value,&length)||!ReadAll(file.value,&h,sizeof(h))||h.magic!=magic||h.identity!=identity||
       h.sourceSize!=source.size()||h.ddsSize<128||h.ddsSize>TextureMaxBytes||
       uint64_t(sizeof(h))+h.sourceSize+h.ddsSize+32!=uint64_t(length.QuadPart))return false;
    Sha hash;hash.Add(&h,sizeof(h));unsigned char buffer[65536];
    for(size_t at=0;at<source.size();){size_t n=(std::min)(sizeof(buffer),source.size()-at);
        if(!ReadAll(file.value,buffer,n)||memcmp(buffer,source.data()+at,n))return false;hash.Add(buffer,n);at+=n;}
    std::vector<unsigned char> dds(h.ddsSize);Digest expected{};
    if(!ReadAll(file.value,dds.data(),dds.size())||!ReadAll(file.value,expected.data(),32))return false;
    hash.Add(dds.data(),dds.size());if(hash.Finish()!=expected)return false;
    TextureUpload layout;if(!PrepareTextureUpload(h.desc,dds,layout))return false;
    generated=std::move(dds);upload=std::move(layout);return true;
}
bool TextureDisk::Write(TextureBytes source,TextureBytes dds,const D3D11_TEXTURE2D_DESC& desc) {
    TextureUpload layout;
    if(directory.empty()||!source.data()||source.size()>TextureMaxBytes||dds.size()>TextureMaxBytes||!PrepareTextureUpload(desc,dds,layout))return false;
    Header h{magic,uint32_t(source.size()),uint32_t(dds.size()),identity,desc};
    const auto target=Path(source),temp=target+L".part";
    // Namespace lock permits safe replacement of a stale temporary from an interrupted run.
    Handle file{CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return false;
    Sha hash;hash.Add(&h,sizeof(h));hash.Add(source.data(),source.size());hash.Add(dds.data(),dds.size());const auto digest=hash.Finish();
    bool ok=WriteAll(file.value,&h,sizeof(h))&&WriteAll(file.value,source.data(),source.size())&&
        WriteAll(file.value,dds.data(),dds.size())&&WriteAll(file.value,digest.data(),32)&&FlushFileBuffers(file.value);
    file.Close();if(ok)ok=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp.c_str());return ok;
}
}
