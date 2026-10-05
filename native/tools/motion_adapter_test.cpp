#include "motion_adapter.hpp"
#include "code_calls.hpp"
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

using namespace vii;
using M = MotionBytes::Mode;
static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)
static constexpr uintptr_t GetCaller = 0x1234, PutCaller = 0x5678;
static const char* path = "/MODEL/CHARA/061/MOTION/BATTLE.cl3";
static uintptr_t manager = 0;
static int getResult = 0, putResult = 0;
static unsigned getCount = 0, putCount = 0;
static bool originalThrow = false;
static uint32_t seenNamespace = 0, seenSize = 0;
static const char* seenPath = nullptr;
static void* seenBytes = nullptr;
static int __cdecl OriginalGet(uint32_t nameSpace, const char* p, void* bytes, uint32_t size) {
    ++getCount; seenNamespace = nameSpace; seenPath = p; seenBytes = bytes; seenSize = size;
    if (originalThrow) throw std::runtime_error("original error");
    if (getResult && bytes && size == 4) std::memcpy(bytes, "GAME", 4);
    return getResult;
}
static int __cdecl OriginalPut(uint32_t nameSpace, const char* p, void* bytes, uint32_t size) {
    ++putCount; seenNamespace = nameSpace; seenPath = p; seenBytes = bytes; seenSize = size;
    return putResult;
}
static void Reset() { manager = 0; getCount = putCount = 0; getResult = putResult = 0; originalThrow = false; }
static void GuardsAndOwnership() {
    Reset();
    MotionAdapter a(M::Replay, 8, OriginalGet, OriginalPut, GetCaller, PutCaller, &manager);
    char source[] = "abcd", destination[] = "zzzz";
    CHECK(a.Put(PutCaller, 3, path, source, 4) == 0 && putCount == 1);
    CHECK(a.Snapshot().stores == 0 && a.Snapshot().enabled == 0);
    a.Enable();
    CHECK(a.Put(PutCaller, 3, path, source, 4) == 0 && putCount == 2);
    CHECK(seenNamespace == 3 && seenPath == path && seenBytes == source && seenSize == 4);
    source[0] = 'x';
    CHECK(a.Get(GetCaller, 3, path, destination, 4) == 1 && getCount == 1);
    CHECK(!std::memcmp(destination, "abcd", 4));
    CHECK(seenNamespace == 3 && seenPath == path && seenBytes == destination && seenSize == 4);
    destination[0] = 'y';
    CHECK(a.Get(GetCaller, 3, path, destination, 4) == 1 && getCount == 2);
    CHECK(!std::memcmp(destination, "abcd", 4));
    getResult = 7;
    CHECK(a.Get(GetCaller, 3, path, destination, 4) == 7 && getCount == 3);
    CHECK(!std::memcmp(destination, "GAME", 4) && a.Snapshot().hits == 2);
    getResult = 0; manager = 1;
    CHECK(a.Get(GetCaller, 3, path, destination, 4) == 0 && getCount == 4);
    CHECK(!std::memcmp(destination, "GAME", 4) && a.Snapshot().hits == 2);
    CHECK(a.Put(PutCaller, 3, path, source, 4) == 0 && a.Snapshot().comparisons == 0);
    manager = 0;
    CHECK(a.Get(0x9999, 3, path, destination, 4) == 0 && getCount == 5);
    CHECK(a.Put(0x9999, 3, path, source, 4) == 0 && a.Snapshot().comparisons == 0);
    putResult = 9;
    CHECK(a.Put(PutCaller, 3, path, source, 4) == 9 && a.Snapshot().comparisons == 0);
    putResult = 0;
    CHECK(a.Put(PutCaller, 3, "/MODEL/CHARA/061/MOTION/IDLE.cl3", source, 4) == 0 && a.Snapshot().stores == 1);
    const auto before = getCount;
    originalThrow = true; bool threw = false;
    try { a.Get(GetCaller, 3, path, destination, 4); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && getCount == before + 1 && a.Snapshot().errors == 0);
    originalThrow = false;
    CHECK(a.Put(PutCaller, 3, path, source, 4) == 0 && a.Snapshot().mismatches == 1);
    CHECK(a.Snapshot().disabled == 1 && a.Snapshot().bytes == 0);
    CHECK(a.Get(GetCaller, 3, path, destination, 4) == 0);
    CHECK(a.Snapshot().originalGets == getCount && a.Snapshot().originalPuts == putCount);
}
static void AuditAndFailures() {
    Reset(); char source[] = "abcd", destination[] = "zzzz";
    MotionAdapter a(M::Audit, 8, OriginalGet, OriginalPut, GetCaller, PutCaller, &manager);
    a.Enable();
    CHECK(a.Put(PutCaller, 0, path, source, 4) == 0);
    CHECK(a.Get(GetCaller, 0, path, destination, 4) == 0);
    CHECK(!std::memcmp(destination, "zzzz", 4));
    CHECK(a.Put(PutCaller, 0, path, source, 4) == 0);
    CHECK(a.Snapshot().hits == 0 && a.Snapshot().comparisons == 1 && a.Snapshot().candidates == 1);
    CHECK(a.Get(GetCaller, 0, path, reinterpret_cast<void*>(1), 9) == 0 && a.Snapshot().rejected == 1);
    CHECK(a.Get(GetCaller, 0, path, reinterpret_cast<void*>(1), 4) == 0);
    CHECK(a.Snapshot().errors == 1 && a.Snapshot().enabled == 0 && a.Snapshot().bytes == 0);
    MotionAdapter b(M::Replay, 8, OriginalGet, OriginalPut, GetCaller, PutCaller, &manager);
    b.Enable();
    CHECK(b.Put(PutCaller, 0, reinterpret_cast<const char*>(1), source, 4) == 0);
    CHECK(b.Snapshot().errors == 1 && b.Snapshot().enabled == 0);
    MotionAdapter c(M::Replay, 8, OriginalGet, OriginalPut, GetCaller, PutCaller, reinterpret_cast<const uintptr_t*>(1));
    c.Enable();
    CHECK(c.Get(GetCaller, 0, path, destination, 4) == 0 && c.Snapshot().errors == 1);
    MotionAdapter d(M::Replay, 8, OriginalGet, OriginalPut, GetCaller, PutCaller, &manager);
    d.Enable();
    CHECK(d.Put(PutCaller, 0, path, source, 4) == 0);
    auto readonly = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(readonly != nullptr); DWORD old = 0;
    CHECK(VirtualProtect(readonly, 4096, PAGE_READONLY, &old));
    CHECK(d.Get(GetCaller, 0, path, readonly, 4) == 0 && d.Snapshot().errors == 1);
    CHECK(VirtualFree(readonly, 0, MEM_RELEASE));
}
static MotionAdapter* live = nullptr;
static int __cdecl WrappedGet(uint32_t n, const char* p, void* bytes, uint32_t size) {
    return live->Get(reinterpret_cast<uintptr_t>(_ReturnAddress()), n, p, bytes, size);
}
static int __cdecl WrappedPut(uint32_t n, const char* p, void* bytes, uint32_t size) {
    return live->Put(reinterpret_cast<uintptr_t>(_ReturnAddress()), n, p, bytes, size);
}
static MotionFileFn MakeThunk(unsigned char* code, MotionFileFn original, MotionFileFn replacement, CallSite& site) {
    code[0] = 0x90;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned char push[] = {0xff,0x74,0x24,0x10}; std::memcpy(code + 1 + 4*i, push, 4);
    }
    site.address = code + 17; site.replacement = reinterpret_cast<void*>(replacement);
    site.address[0] = 0xe8;
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(original) - reinterpret_cast<uintptr_t>(site.address) - 5);
    std::memcpy(site.address + 1, &relative, 4);
    const unsigned char tail[] = {0x83,0xc4,0x10,0xc3}; std::memcpy(site.address + 5, tail, 4);
    std::memcpy(site.expected.data(), site.address, 8);
    return reinterpret_cast<MotionFileFn>(code);
}
static void IntegratedCalls() {
    Reset();
    auto page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page != nullptr);
    std::array<CallSite, 2> sites{};
    auto get = MakeThunk(page + 0x20, OriginalGet, WrappedGet, sites[0]);
    auto put = MakeThunk(page + 0x60, OriginalPut, WrappedPut, sites[1]);
    MotionAdapter a(M::Replay, 8, OriginalGet, OriginalPut,
        reinterpret_cast<uintptr_t>(sites[0].address + 5), reinterpret_cast<uintptr_t>(sites[1].address + 5), &manager);
    live = &a;
    DWORD old = 0; CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
    CHECK(FlushInstructionCache(GetCurrentProcess(), page, 4096));
    const auto r = ReplaceTwoCalls(sites);
    CHECK(r.installed && r.status == CallStatus::Installed && !r.resumeFailures);
    char source[] = "abcd", destination[] = "zzzz";
    CHECK(put(5, path, source, 4) == 0 && a.Snapshot().stores == 0);
    a.Enable();
    CHECK(put(5, path, source, 4) == 0 && putCount == 2 && a.Snapshot().stores == 1);
    CHECK(get(5, path, destination, 4) == 1 && getCount == 1 && !std::memcmp(destination, "abcd", 4));
    CHECK(OriginalPut(5, path, source, 4) == 0 && a.Snapshot().comparisons == 0);
    CHECK(WrappedGet(5, path, destination, 4) == 0 && a.Snapshot().hits == 1);
    CHECK(a.Snapshot().version == 1 && a.Snapshot().mode == 2);
    CHECK(VirtualFree(page, 0, MEM_RELEASE)); live = nullptr;
}
int main() {
    GuardsAndOwnership(); AuditAndFailures(); IntegratedCalls();
    std::printf("PASS: %u checks; original-once, unchanged arguments/results, manager/caller guards, audit, owned replay, failure bypass, live two-call adapter\n", checks);
    return 0;
}
