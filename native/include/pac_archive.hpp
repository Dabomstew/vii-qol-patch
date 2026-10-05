#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace vii::pac {
struct Entry {
    std::string name;
    uint32_t packed = 0, unpacked = 0, compression = 0;
    uint64_t offset = 0;
};
struct Archive {
    std::filesystem::path path;
    std::vector<Entry> entries;
    uint64_t size = 0, tableBytes = 0;
};
// Canonical Windows-safe ASCII paths. Logical game requests may start with '/'.
std::string NormalizePath(const std::string& name, bool request = false);
std::string ArchiveNamespace(const std::filesystem::path& relativePac);
std::string ManagerNamespace(const std::string& basename);
Archive Inspect(const std::filesystem::path& path);
using Sink = std::function<void(const unsigned char*, size_t)>;
using Cancel = std::function<bool()>;
void Decode(std::istream& file, const Entry& entry, const Sink& sink, const Cancel& cancel = {});
std::vector<unsigned char> DecodeHuffman(const unsigned char* data, size_t bytes, size_t outputBytes);
}
