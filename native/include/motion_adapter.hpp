#pragma once
#include "patch.hpp"
#include "motion_bytes.hpp"
#include <atomic>

namespace vii {
using MotionFileFn = int (__cdecl*)(uint32_t, const char*, void*, uint32_t);
struct MotionTelemetry {
    uint64_t version, mode, enabled, errors, originalGets, originalPuts, bypasses;
    uint64_t budget, bytes, peak, entries, hits, candidates, misses, stores;
    uint64_t comparisons, mismatches, evictions, rejected, disabled;
};
static_assert(sizeof(MotionTelemetry) == 160, "Stable diagnostic layout");
class MotionAdapter {
public:
    MotionAdapter(MotionBytes::Mode mode, size_t budget, MotionFileFn get, MotionFileFn put,
                  uintptr_t getCaller, uintptr_t putCaller, const uintptr_t* gameCache);
    int Get(uintptr_t caller, uint32_t nameSpace, const char* path, void* bytes, uint32_t size);
    int Put(uintptr_t caller, uint32_t nameSpace, const char* path, void* bytes, uint32_t size);
    void Enable() noexcept { enabled_.store(true); }
    MotionTelemetry Snapshot() const;
private:
    bool Prepare(uintptr_t caller, uintptr_t expected, int original, const char* path,
                 void* bytes, uint32_t size, bool write, char (&copy)[257]);
    void Fail() noexcept;
    MotionBytes cache_;
    MotionBytes::Mode mode_;
    MotionFileFn get_, put_;
    uintptr_t getCaller_, putCaller_;
    const uintptr_t* gameCache_;
    const size_t budget_;
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> gets_{0}, puts_{0}, bypasses_{0}, errors_{0};
};
}
