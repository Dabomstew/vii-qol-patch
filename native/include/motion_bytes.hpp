#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace vii {
// Owned raw file bytes only. Integration must first call the original cache
// function and require its miss, the verified caller, and a null game cache.
class MotionBytes {
public:
    enum class Mode { Audit, Replay };
    enum class Result { Miss, Candidate, Hit, Stored, Verified, Rejected, Mismatch, Disabled };
    struct Allocator {
        void* context;
        void* (*allocate)(void*, size_t) noexcept;
        void (*release)(void*, void*) noexcept;
    };
    struct Stats {
        size_t budget = 0, bytes = 0, peak = 0, entries = 0;
        uint64_t hits = 0, candidates = 0, misses = 0, stores = 0;
        uint64_t comparisons = 0, mismatches = 0, evictions = 0, rejected = 0;
        bool disabled = false, closed = false;
    };
    explicit MotionBytes(Mode mode, size_t budget);
    MotionBytes(Mode mode, size_t budget, Allocator allocator);
    ~MotionBytes();
    MotionBytes(const MotionBytes&) = delete;
    MotionBytes& operator=(const MotionBytes&) = delete;
    Result Get(uint32_t nameSpace, const char* path, void* destination, size_t size);
    Result Put(uint32_t nameSpace, const char* path, const void* source, size_t size);
    Stats Snapshot() const;
    void Close();
    static bool SupportsPath(const char* path) { return EligiblePath(path) != nullptr; }
private:
    struct Entry {
        std::array<char, 256> path{};
        uint32_t nameSpace = 0;
        void* bytes = nullptr;
        size_t size = 0;
    };
    static const char* EligiblePath(const char* path);
    size_t Find(uint32_t nameSpace, const char* path) const;
    void Touch(size_t index);
    void Remove(size_t index);
    void Clear();
    void Mismatch();
    Mode mode_;
    Allocator allocator_;
    mutable std::mutex mutex_;
    Stats stats_;
    // Oldest first; fixed metadata avoids unbounded key allocations.
    std::array<Entry, 128> entries_{};
};
}
