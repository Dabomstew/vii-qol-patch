#include "motion_bytes.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace vii {
namespace {
void* Allocate(void*, size_t size) noexcept { return std::malloc(size); }
void Release(void*, void* p) noexcept { std::free(p); }
char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool Consume(const char*& p, const char* literal) {
    while (*literal) {
        if (Lower(*p) != *literal++) return false;
        ++p;
    }
    return true;
}
}
MotionBytes::MotionBytes(Mode mode, size_t budget)
    : MotionBytes(mode, budget, {nullptr, Allocate, Release}) {}
MotionBytes::MotionBytes(Mode mode, size_t budget, Allocator allocator)
    : mode_(mode), allocator_(allocator) {
    if ((mode != Mode::Audit && mode != Mode::Replay) || !budget || budget > 64u * 1024u * 1024u ||
        !allocator.allocate || !allocator.release) throw std::invalid_argument("motion cache configuration");
    stats_.budget = budget;
}
MotionBytes::~MotionBytes() { Clear(); }
const char* MotionBytes::EligiblePath(const char* path) {
    // Caller supplies readable, NUL-terminated memory. This bound limits keys;
    // it is not a substitute for validating game pointers in the hook adapter.
    if (!path) return nullptr;
    if (*path == '/') ++path;
    size_t length = 0;
    while (length < 256 && path[length]) ++length;
    if (length == 256) return nullptr;
    const char* p = path;
    if (!Consume(p, "model/chara/")) return nullptr;
    if (*p < '0' || *p > '9') return nullptr;
    do { ++p; } while (*p >= '0' && *p <= '9');
    return Consume(p, "/motion/battle.cl3") && !*p ? path : nullptr;
}
size_t MotionBytes::Find(uint32_t nameSpace, const char* path) const {
    for (size_t i = 0; i < stats_.entries; ++i)
        if (entries_[i].nameSpace == nameSpace && !std::strcmp(entries_[i].path.data(), path)) return i;
    return stats_.entries;
}
void MotionBytes::Touch(size_t index) {
    auto entry = entries_[index];
    for (size_t i = index + 1; i < stats_.entries; ++i) entries_[i - 1] = entries_[i];
    entries_[stats_.entries - 1] = entry;
}
void MotionBytes::Remove(size_t index) {
    allocator_.release(allocator_.context, entries_[index].bytes);
    stats_.bytes -= entries_[index].size;
    for (size_t i = index + 1; i < stats_.entries; ++i) entries_[i - 1] = entries_[i];
    entries_[--stats_.entries] = Entry{};
}
void MotionBytes::Clear() { while (stats_.entries) Remove(stats_.entries - 1); }
void MotionBytes::Mismatch() { ++stats_.mismatches; stats_.disabled = true; Clear(); }
MotionBytes::Result MotionBytes::Get(uint32_t nameSpace, const char* path, void* destination, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.disabled || stats_.closed) return Result::Disabled;
    path = EligiblePath(path);
    if (!path || !destination || !size || size > stats_.budget) { ++stats_.rejected; return Result::Rejected; }
    const size_t index = Find(nameSpace, path);
    if (index == stats_.entries || entries_[index].size != size) { ++stats_.misses; return Result::Miss; }
    Touch(index); ++stats_.candidates;
    if (mode_ == Mode::Audit) return Result::Candidate;
    const auto& entry = entries_[stats_.entries - 1];
    std::memcpy(destination, entry.bytes, size);
    if (std::memcmp(destination, entry.bytes, size)) { Mismatch(); return Result::Mismatch; }
    ++stats_.hits;
    return Result::Hit;
}
MotionBytes::Result MotionBytes::Put(uint32_t nameSpace, const char* path, const void* source, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.disabled || stats_.closed) return Result::Disabled;
    path = EligiblePath(path);
    if (!path || !source || !size || size > stats_.budget) { ++stats_.rejected; return Result::Rejected; }
    const size_t index = Find(nameSpace, path);
    if (index != stats_.entries) {
        ++stats_.comparisons;
        if (entries_[index].size != size || std::memcmp(entries_[index].bytes, source, size)) {
            Mismatch(); return Result::Mismatch;
        }
        Touch(index); return Result::Verified;
    }
    while (stats_.entries && (size > stats_.budget - stats_.bytes || stats_.entries == entries_.size())) {
        Remove(0); ++stats_.evictions;
    }
    void* bytes = allocator_.allocate(allocator_.context, size);
    if (!bytes) { ++stats_.rejected; return Result::Rejected; }
    std::memcpy(bytes, source, size);
    auto& entry = entries_[stats_.entries++];
    std::memcpy(entry.path.data(), path, std::strlen(path) + 1);
    entry.nameSpace = nameSpace; entry.bytes = bytes; entry.size = size;
    stats_.bytes += size; ++stats_.stores;
    if (stats_.bytes > stats_.peak) stats_.peak = stats_.bytes;
    return Result::Stored;
}
MotionBytes::Stats MotionBytes::Snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return stats_; }
void MotionBytes::Close() { std::lock_guard<std::mutex> lock(mutex_); stats_.closed = true; Clear(); }
}
