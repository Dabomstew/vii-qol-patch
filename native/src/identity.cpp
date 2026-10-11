#include "patch.hpp"
#include <bcrypt.h>
#include <fstream>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
namespace vii {
namespace {
BCRYPT_ALG_HANDLE Sha256Algorithm() {
    // Shared immutable provider, pinned for process lifetime. Each call still
    // owns its hash object, so concurrent source-key hashing is independent.
    static const BCRYPT_ALG_HANDLE algorithm=[] {
        BCRYPT_ALG_HANDLE handle=nullptr;
        if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("SHA256 unavailable");
        return handle;
    }();
    return algorithm;
}
}
std::wstring ModulePath(HMODULE module) {
    std::vector<wchar_t> path(32768);
    auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if (!n || n>=path.size()) throw std::runtime_error("module path unavailable");
    return std::wstring(path.data(),n);
}
Digest Hash(const void* data,size_t size) {
    if(size>0xffffffffull) throw std::runtime_error("hash input too large");
    BCRYPT_HASH_HANDLE hash=nullptr;
    Digest result{};
    auto status=BCryptCreateHash(Sha256Algorithm(),&hash,nullptr,0,nullptr,0,0);
    if(status>=0) status=BCryptHashData(hash,(PUCHAR)data,static_cast<ULONG>(size),0);
    if(status>=0) status=BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0);
    if(hash) BCryptDestroyHash(hash);
    if(status<0) throw std::runtime_error("SHA256 failed");
    return result;
}
Digest HashFile(const std::wstring& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("cannot open identity file");
    auto size=file.tellg(); if(size<0 || size>128*1024*1024) throw std::runtime_error("identity file size");
    std::vector<char> data(static_cast<size_t>(size)); file.seekg(0);
    if(!file.read(data.data(),static_cast<std::streamsize>(data.size()))) throw std::runtime_error("identity file read");
    return Hash(data.data(),data.size());
}
}
