#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vii::loose {
using Digest = std::array<unsigned char,32>;
class Sha256 {
    BCRYPT_HASH_HANDLE handle = nullptr;
public:
    Sha256(); ~Sha256();
    Sha256(const Sha256&) = delete; Sha256& operator=(const Sha256&) = delete;
    void Add(const void* data,size_t bytes);
    Digest Finish();
};
struct Source {
    std::string name, group;
    uint64_t size = 0, stamp = 0, tableBytes = 0;
    Digest tableHash{};
};
struct File {
    uint32_t source = 0;
    std::string name;
    uint64_t size = 0;
    Digest hash{};
};
struct Manifest { std::vector<Source> sources; std::vector<File> files; };
std::filesystem::path Extended(const std::filesystem::path& path);
// Derived from the one configured output path; no separate DLC setting.
std::filesystem::path DlcOutput(const std::filesystem::path& output);
Digest HashFile(const std::filesystem::path& path,uint64_t prefix = UINT64_MAX);
Source Identify(const std::filesystem::path& path,uint64_t tableBytes);
bool Matches(const std::filesystem::path& path,const Source& source);
Manifest ReadManifest(const std::filesystem::path& path);
void WriteManifest(const std::filesystem::path& path,const Manifest& manifest);
// Reject junctions/symlinks inside an owned output tree.
void CheckOutputPath(const std::filesystem::path& root,const std::filesystem::path& relative);
}
