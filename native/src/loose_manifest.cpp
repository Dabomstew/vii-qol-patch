#include "loose_manifest.hpp"
#include "pac_archive.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#pragma comment(lib,"bcrypt.lib")

namespace vii::loose {
namespace fs=std::filesystem;
namespace {
[[noreturn]] void Fail(const char* message){throw std::runtime_error(message);}
BCRYPT_ALG_HANDLE Algorithm(){
    static BCRYPT_ALG_HANDLE algorithm=[] {BCRYPT_ALG_HANDLE a=nullptr;if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)Fail("SHA256 initialization failed");return a;}();
    return algorithm;
}
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
void Put(std::vector<unsigned char>& data,uint64_t value,unsigned bytes){while(bytes--){data.push_back(static_cast<unsigned char>(value));value>>=8;}}
void Text(std::vector<unsigned char>& data,const std::string& text){
    if(text.size()>259)Fail("Manifest path too long");Put(data,text.size(),2);data.insert(data.end(),text.begin(),text.end());
}
struct Reader {
    const std::vector<unsigned char>& data;size_t pos=8;
    uint64_t Get(unsigned bytes){if(bytes>8||pos>data.size()||bytes>data.size()-pos)Fail("Truncated manifest");uint64_t v=0;for(unsigned i=0;i<bytes;++i)v|=uint64_t(data[pos++])<<(i*8);return v;}
    std::string Text(){const size_t size=size_t(Get(2));if(size>259||pos>data.size()||size>data.size()-pos)Fail("Invalid manifest path");std::string s(reinterpret_cast<const char*>(data.data()+pos),size);pos+=size;return pac::NormalizePath(s);}
    Digest Hash(){Digest value{};if(pos>data.size()||value.size()>data.size()-pos)Fail("Truncated manifest digest");std::memcpy(value.data(),data.data()+pos,value.size());pos+=value.size();return value;}
};
}

Sha256::Sha256(){if(BCryptCreateHash(Algorithm(),&handle,nullptr,0,nullptr,0,0)<0)Fail("SHA256 creation failed");}
Sha256::~Sha256(){if(handle)BCryptDestroyHash(handle);}
void Sha256::Add(const void* data,size_t bytes){
    if(bytes>ULONG_MAX||BCryptHashData(handle,(PUCHAR)data,static_cast<ULONG>(bytes),0)<0)Fail("SHA256 update failed");
}
Digest Sha256::Finish(){Digest value{};if(BCryptFinishHash(handle,value.data(),ULONG(value.size()),0)<0)Fail("SHA256 finish failed");return value;}
fs::path Extended(const fs::path& path){
    auto name=fs::absolute(path).lexically_normal().wstring();
    if(name.rfind(L"\\\\?\\",0)==0)return name;
    if(name.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+name.substr(2);
    return L"\\\\?\\"+name;
}
fs::path DlcOutput(const fs::path& output){
    auto root=fs::absolute(output).lexically_normal();
    if(root.filename().empty())root=root.parent_path();
    return fs::path(root.wstring()+L".dlc");
}
Digest HashFile(const fs::path& path,uint64_t prefix){
    Handle file{CreateFileW(Extended(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(file.h==INVALID_HANDLE_VALUE)Fail("Cannot open file for verification");
    LARGE_INTEGER length{};if(!GetFileSizeEx(file.h,&length)||length.QuadPart<0)Fail("Cannot size verification file");
    if(prefix==UINT64_MAX)prefix=uint64_t(length.QuadPart);
    if(prefix>uint64_t(length.QuadPart))Fail("Verification prefix exceeds file");
    Sha256 hash;std::vector<unsigned char> bytes(1024*1024);
    while(prefix){const DWORD want=DWORD(std::min<uint64_t>(prefix,bytes.size()));DWORD got=0;if(!ReadFile(file.h,bytes.data(),want,&got,nullptr)||got!=want)Fail("Verification read failed");hash.Add(bytes.data(),got);prefix-=got;}
    return hash.Finish();
}
Source Identify(const fs::path& path,uint64_t tableBytes){
    WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(Extended(path).c_str(),GetFileExInfoStandard,&info)||info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)Fail("Cannot identify source PAC");
    Source source;source.size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    source.stamp=(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
    source.tableBytes=tableBytes;source.tableHash=HashFile(path,tableBytes);return source;
}
bool Matches(const fs::path& path,const Source& source){
    try {const auto current=Identify(path,source.tableBytes);return current.size==source.size&&current.stamp==source.stamp&&current.tableHash==source.tableHash;}catch(...){return false;}
}
void CheckOutputPath(const fs::path& root,const fs::path& relative){
    if(relative.is_absolute())Fail("Output path must be relative");
    auto current=Extended(root);
    auto check=[&]{DWORD attrs=GetFileAttributesW(current.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))Fail("Output tree contains a junction or symbolic link");};
    check();for(const auto& part:relative){if(part==L"..")Fail("Output traversal refused");current/=part;check();}
}
Manifest ReadManifest(const fs::path& path){
    const auto size=fs::file_size(Extended(path));if(size<48||size>128*1024*1024)Fail("Invalid manifest size");
    std::ifstream input(Extended(path),std::ios::binary);std::vector<unsigned char> data(static_cast<size_t>(size));
    if(!input.read(reinterpret_cast<char*>(data.data()),data.size()))Fail("Cannot read manifest");
    Digest expected{};std::memcpy(expected.data(),data.data()+data.size()-32,32);data.resize(data.size()-32);
    Sha256 hash;hash.Add(data.data(),data.size());if(hash.Finish()!=expected||std::memcmp(data.data(),"VIIUL001",8))Fail("Manifest integrity/version mismatch");
    Reader reader{data};Manifest m;const auto sources=reader.Get(4),files=reader.Get(4);
    if(!sources||sources>65536||files>1000000)Fail("Manifest count exceeds bound");
    m.sources.reserve(size_t(sources));m.files.reserve(size_t(files));std::unordered_set<std::string> names;
    for(uint64_t i=0;i<sources;++i){Source s;s.name=reader.Text();s.group=reader.Text();s.size=reader.Get(8);s.stamp=reader.Get(8);s.tableBytes=reader.Get(8);s.tableHash=reader.Hash();
        if(s.tableBytes<20||s.tableBytes>20+65536*288||s.tableBytes>s.size||pac::ArchiveNamespace(fs::path(s.name))!=s.group||!names.insert(s.name).second)Fail("Invalid source manifest entry");m.sources.push_back(std::move(s));}
    names.clear();for(uint64_t i=0;i<files;++i){File f;f.source=uint32_t(reader.Get(4));f.name=reader.Text();f.size=reader.Get(8);f.hash=reader.Hash();
        if(f.source>=m.sources.size()||f.size>UINT32_MAX||!names.insert(m.sources[f.source].group+"/"+f.name).second)Fail("Invalid/duplicate output manifest entry");m.files.push_back(std::move(f));}
    if(reader.pos!=data.size())Fail("Unexpected manifest trailing data");return m;
}
void WriteManifest(const fs::path& path,const Manifest& m){
    std::vector<unsigned char> data={'V','I','I','U','L','0','0','1'};Put(data,m.sources.size(),4);Put(data,m.files.size(),4);
    for(const auto& s:m.sources){Text(data,s.name);Text(data,s.group);Put(data,s.size,8);Put(data,s.stamp,8);Put(data,s.tableBytes,8);data.insert(data.end(),s.tableHash.begin(),s.tableHash.end());}
    for(const auto& f:m.files){Put(data,f.source,4);Text(data,f.name);Put(data,f.size,8);data.insert(data.end(),f.hash.begin(),f.hash.end());}
    Sha256 hash;hash.Add(data.data(),data.size());const auto digest=hash.Finish();data.insert(data.end(),digest.begin(),digest.end());
    std::ofstream file(Extended(path),std::ios::binary|std::ios::trunc);if(!file.write(reinterpret_cast<const char*>(data.data()),data.size())||!file.flush())Fail("Cannot write completion manifest");
}
}
