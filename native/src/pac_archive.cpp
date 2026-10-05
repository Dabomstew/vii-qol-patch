#include "pac_archive.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace vii::pac {
namespace {
[[noreturn]] void Fail(const char* message) { throw std::runtime_error(message); }
uint32_t U32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void Read(std::istream& f, uint64_t offset, void* data, size_t count) {
    if (offset > uint64_t(std::numeric_limits<std::streamoff>::max())) Fail("PAC offset overflow");
    f.clear(); f.seekg(static_cast<std::streamoff>(offset));
    if (!f.read(static_cast<char*>(data), static_cast<std::streamsize>(count))) Fail("Truncated PAC data");
}
class Bits {
    const unsigned char* data; size_t bytes, cursor = 0;
public:
    Bits(const unsigned char* p, size_t n) : data(p), bytes(n) {}
    size_t Remaining() const { return bytes * 8 - cursor; }
    unsigned Get(unsigned n) {
        if (n > 16 || Remaining() < n) Fail("Truncated Huffman bitstream");
        unsigned result = 0;
        while (n--) { result = (result << 1) | ((data[cursor / 8] >> (7 - cursor % 8)) & 1); ++cursor; }
        return result;
    }
    unsigned Peek10() const {
        const size_t i = cursor / 8; const unsigned shift = unsigned(cursor % 8);
        unsigned value = unsigned(data[i]) << 16;
        if (i + 1 < bytes) value |= unsigned(data[i + 1]) << 8;
        if (i + 2 < bytes) value |= data[i + 2];
        return (value >> (14 - shift)) & 1023;
    }
    void Advance(unsigned n) { cursor += n; }
};
struct Node { int left = -1, right = -1; };
int Tree(Bits& bits, std::array<Node, 511>& nodes, int& next, unsigned depth) {
    if (depth > 255) Fail("Huffman tree is too deep");
    if (!bits.Get(1)) return int(bits.Get(8));
    if (next >= int(nodes.size())) Fail("Huffman tree has too many branches");
    const int index = next++;
    nodes[index].left = Tree(bits, nodes, next, depth + 1);
    nodes[index].right = Tree(bits, nodes, next, depth + 1);
    return index;
}
}

std::string NormalizePath(const std::string& value, bool request) {
    if (value.empty() || value.size() >= 260) Fail("Empty or overlong asset path");
    std::string name = value;
    std::replace(name.begin(), name.end(), '\\', '/');
    if (request && name.front() == '/') name.erase(0, 1);
    if (name.empty() || name.front() == '/' || name.back() == '/') Fail("Absolute or empty asset path");
    size_t start = 0;
    while (start < name.size()) {
        const auto end = name.find('/', start); const auto length = (end == std::string::npos ? name.size() : end) - start;
        if (!length) Fail("Empty path component");
        auto part = name.substr(start, length);
        if (part == "." || part == ".." || part.back() == '.' || part.back() == ' ') Fail("Unsafe path component");
        for (size_t i = start; i < start + length; ++i) {
            unsigned char c = name[i];
            if (c < 32 || c >= 127 || std::strchr(":*?\"<>|", c)) Fail("Unsupported asset path character");
            if (c >= 'A' && c <= 'Z') name[i] = char(c + 'a' - 'A');
        }
        part = name.substr(start, length); const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '1' && stem[3] <= '9'))
            Fail("Reserved Windows filename");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return name;
}

std::string ArchiveNamespace(const std::filesystem::path& relativePac) {
    auto value = relativePac.generic_string();
    if (value.size() < 10) Fail("PAC name must end in five part digits and .pac");
    std::string extension = value.substr(value.size() - 4);
    for (auto& c : extension) if (c >= 'A' && c <= 'Z') c = char(c + 'a' - 'A');
    if (extension != ".pac") Fail("Not a PAC archive");
    for (size_t i = value.size() - 9; i < value.size() - 4; ++i)
        if (value[i] < '0' || value[i] > '9') Fail("PAC name lacks five part digits");
    return NormalizePath(value.substr(0, value.size() - 9));
}

std::string ManagerNamespace(const std::string& basename) {
    auto name = NormalizePath(basename);
    // Keep the root in the lookup key: DLC and base-game archives may have
    // identical group or entry names but must never share loose-file routing.
    if (name.rfind("contents/",0)!=0 && name.rfind("dlc/",0)!=0) Fail("Unsupported game archive namespace");
    return name;
}

Archive Inspect(const std::filesystem::path& path) {
    Archive result; result.path = path; result.size = std::filesystem::file_size(path);
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 20> header{}; Read(file, 0, header.data(), header.size());
    ArchiveNamespace(path.filename());
    const auto stem = path.stem().string(); const auto part = std::stoul(stem.substr(stem.size() - 5));
    if (std::memcmp(header.data(), "DW_PACK\0", 8) || U32(header.data() + 8) || U32(header.data() + 16) != part) Fail("Unsupported DW_PACK header or part number");
    const uint32_t count = U32(header.data() + 12);
    if (!count || count > 65536) Fail("Invalid PAC entry count");
    result.tableBytes = 20 + uint64_t(count) * 288;
    if (result.tableBytes > result.size) Fail("Truncated PAC index");
    std::vector<unsigned char> table(size_t(count) * 288); Read(file, 20, table.data(), table.size());
    result.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto p = table.data() + size_t(i) * 288;
        const auto end = static_cast<const unsigned char*>(std::memchr(p + 8, 0, 260));
        if (!end) Fail("Unterminated PAC asset name");
        Entry e; e.name = NormalizePath(std::string(reinterpret_cast<const char*>(p + 8), size_t(end - p - 8)));
        e.packed = U32(p + 272); e.unpacked = U32(p + 276); e.compression = U32(p + 280);
        e.offset = result.tableBytes + U32(p + 284);
        if (e.compression > 1 || (e.compression == 0 && e.packed != e.unpacked)) Fail("Unsupported PAC compression");
        if (e.offset > result.size || e.packed > result.size - e.offset) Fail("PAC asset extends outside archive");
        result.entries.push_back(std::move(e));
    }
    return result;
}

std::vector<unsigned char> DecodeHuffman(const unsigned char* data, size_t bytes, size_t outputBytes) {
    if (bytes > 16 * 1024 * 1024 || outputBytes > 16 * 1024 * 1024) Fail("Huffman block exceeds bound");
    if (!outputBytes) return {};
    Bits bits(data, bytes); std::array<Node, 511> nodes{}; int next = 256;
    const int root = Tree(bits, nodes, next, 0);
    struct Fast { int node; unsigned used; };
    std::array<Fast, 1024> table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        int node = root; unsigned used = 0;
        while (node > 255 && used < 10) { node = ((i >> (9 - used)) & 1) ? nodes[node].right : nodes[node].left; ++used; }
        table[i] = {node, used};
    }
    std::vector<unsigned char> output(outputBytes);
    for (auto& byte : output) {
        int node = root;
        if (bits.Remaining() >= 10) { const auto f = table[bits.Peek10()]; bits.Advance(f.used); node = f.node; }
        while (node > 255) node = bits.Get(1) ? nodes[node].right : nodes[node].left;
        byte = static_cast<unsigned char>(node);
    }
    return output;
}

void Decode(std::istream& file, const Entry& entry, const Sink& sink, const Cancel& cancel) {
    auto check = [&] { if (cancel && cancel()) Fail("Cancelled"); };
    check();
    if (!entry.compression) {
        std::vector<unsigned char> bytes(1024 * 1024);
        for (uint64_t done = 0; done < entry.unpacked;) {
            check(); const auto n = size_t(std::min<uint64_t>(bytes.size(), entry.unpacked - done));
            Read(file, entry.offset + done, bytes.data(), n); sink(bytes.data(), n); done += n;
        }
        return;
    }
    if (entry.packed < 16) Fail("Truncated Huffman header");
    std::array<unsigned char, 16> header{}; Read(file, entry.offset, header.data(), header.size());
    const auto count = U32(header.data() + 4), blockSize = U32(header.data() + 8), headerSize = U32(header.data() + 12);
    if (U32(header.data()) != 0x1234 || !blockSize || blockSize > 16 * 1024 * 1024 ||
        count > 1048576 || uint64_t(count) * 12 + 16 != headerSize || headerSize > entry.packed ||
        count != (uint64_t(entry.unpacked) + blockSize - 1) / blockSize) Fail("Invalid divided-Huffman header");
    std::vector<unsigned char> descriptors(size_t(count) * 12);
    Read(file, entry.offset + 16, descriptors.data(), descriptors.size());
    uint64_t written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        check(); const auto d = descriptors.data() + size_t(i) * 12;
        const uint32_t out = U32(d), in = U32(d + 4), offset = U32(d + 8);
        const auto expected = uint32_t(std::min<uint64_t>(blockSize, entry.unpacked - written));
        if (out != expected || !in || in > 16 * 1024 * 1024 || uint64_t(headerSize) + offset + in > entry.packed) Fail("Invalid Huffman block descriptor");
        std::vector<unsigned char> encoded(in); Read(file, entry.offset + headerSize + offset, encoded.data(), in);
        auto decoded = DecodeHuffman(encoded.data(), encoded.size(), out); sink(decoded.data(), decoded.size()); written += out;
    }
    if (written != entry.unpacked) Fail("PAC decoded size mismatch");
}
}
