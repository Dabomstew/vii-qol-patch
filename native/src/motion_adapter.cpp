#include "motion_adapter.hpp"
#include <stdexcept>

namespace vii {
namespace {
bool ReadManager(const uintptr_t* address, uintptr_t& value) noexcept {
    __try { value = *address; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CopyPath(const char* source, char* destination) noexcept {
    __try {
        if (!source) return false;
        for (size_t i = 0; i < 257; ++i) {
            destination[i] = source[i];
            if (!destination[i]) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Accessible(void* pointer, size_t size, bool write) noexcept {
    auto current = reinterpret_cast<uintptr_t>(pointer);
    if (!current || !size || size > UINTPTR_MAX - current) return false;
    const auto end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION m{};
        if (VirtualQuery(reinterpret_cast<void*>(current), &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protect = m.Protect & 0xff;
        const bool writable = protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
            protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
        const bool readable = writable || protect == PAGE_READONLY || protect == PAGE_EXECUTE_READ;
        if (!readable || (write && !writable)) return false;
        const auto base = reinterpret_cast<uintptr_t>(m.BaseAddress);
        if (m.RegionSize > UINTPTR_MAX - base || base + m.RegionSize <= current) return false;
        current = base + m.RegionSize;
    }
    return true;
}
}
MotionAdapter::MotionAdapter(MotionBytes::Mode mode, size_t budget, MotionFileFn get, MotionFileFn put,
                             uintptr_t getCaller, uintptr_t putCaller, const uintptr_t* gameCache)
    : cache_(mode, budget), mode_(mode), get_(get), put_(put), getCaller_(getCaller), putCaller_(putCaller), gameCache_(gameCache), budget_(budget) {
    if (!get || !put || !getCaller || !putCaller || !gameCache) throw std::invalid_argument("motion adapter configuration");
}
void MotionAdapter::Fail() noexcept {
    ++errors_; enabled_.store(false);
    try { cache_.Close(); } catch (...) {}
}
bool MotionAdapter::Prepare(uintptr_t caller, uintptr_t expected, int original, const char* path,
                            void* bytes, uint32_t size, bool write, char (&copy)[257]) {
    if (!enabled_.load() || caller != expected || original != 0) { ++bypasses_; return false; }
    uintptr_t manager = 0;
    if (!ReadManager(gameCache_, manager)) { Fail(); return false; }
    if (manager) { ++bypasses_; return false; }
    if (!CopyPath(path, copy)) { Fail(); return false; }
    if (!MotionBytes::SupportsPath(copy)) { ++bypasses_; return false; }
    // Oversized/zero payloads are rejected by the cache without dereferencing.
    // Valid game buffers remain owned/stable for the duration of this call.
    if (size && size <= budget_ && !Accessible(bytes, size, write)) { Fail(); return false; }
    return true;
}
int MotionAdapter::Get(uintptr_t caller, uint32_t nameSpace, const char* path, void* bytes, uint32_t size) {
    ++gets_;
    const int original = get_(nameSpace, path, bytes, size); // Never caught/retried by the adapter.
    try {
        char copy[257];
        if (Prepare(caller, getCaller_, original, path, bytes, size, true, copy) &&
            cache_.Get(nameSpace, copy, bytes, size) == MotionBytes::Result::Hit) return 1;
    } catch (...) { Fail(); }
    return original;
}
int MotionAdapter::Put(uintptr_t caller, uint32_t nameSpace, const char* path, void* bytes, uint32_t size) {
    ++puts_;
    const int original = put_(nameSpace, path, bytes, size);
    try {
        char copy[257];
        if (Prepare(caller, putCaller_, original, path, bytes, size, false, copy)) cache_.Put(nameSpace, copy, bytes, size);
    } catch (...) { Fail(); }
    return original;
}
MotionTelemetry MotionAdapter::Snapshot() const {
    const auto s = cache_.Snapshot();
    return {1, mode_ == MotionBytes::Mode::Audit ? 1ull : 2ull, enabled_.load() ? 1ull : 0ull,
        errors_.load(), gets_.load(), puts_.load(), bypasses_.load(),
        s.budget, s.bytes, s.peak, s.entries, s.hits, s.candidates, s.misses, s.stores,
        s.comparisons, s.mismatches, s.evictions, s.rejected, (s.disabled || s.closed) ? 1ull : 0ull};
}
}
