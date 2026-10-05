#include "code_calls.hpp"
#include <winternl.h>
#include <cstring>

namespace vii {
namespace {
static_assert(sizeof(void*) == 4, "Only the verified x86 call encoding is supported");
constexpr size_t MaxThreads = 256;
constexpr ULONG CensusBytes = 16u * 1024u * 1024u;
using QueryFn = NTSTATUS (NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
struct Peer { DWORD id = 0; HANDLE handle = nullptr; bool suspended = false; };
#ifdef VII_CODE_CALLS_TESTING
CallFault fault = CallFault::None;
bool Fail(CallFault value) { if (fault != value) return false; fault = CallFault::None; return true; }
#define INJECT(name) Fail(CallFault::name)
#else
#define INJECT(name) false
#endif
struct Census {
    QueryFn query = nullptr;
    void* buffer = nullptr;
    std::array<DWORD, MaxThreads> ids{};
    size_t count = 0;
    CallStatus Read(DWORD pid) noexcept {
        count = 0;
        ULONG returned = 0;
        if (INJECT(Census) || query(SystemProcessInformation, buffer, CensusBytes, &returned) < 0 ||
            !returned || returned > CensusBytes) return CallStatus::CensusFailed;
        size_t offset = 0;
        while (offset <= returned && returned - offset >= sizeof(SYSTEM_PROCESS_INFORMATION)) {
            const auto p = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(static_cast<const unsigned char*>(buffer) + offset);
            const size_t span = p->NextEntryOffset ? p->NextEntryOffset : returned - offset;
            if (span < sizeof(*p) || span > returned - offset) return CallStatus::CensusFailed;
            if (reinterpret_cast<uintptr_t>(p->UniqueProcessId) == pid) {
                if (!p->NumberOfThreads || p->NumberOfThreads > MaxThreads) return CallStatus::ThreadLimit;
                if (p->NumberOfThreads > (span - sizeof(*p)) / sizeof(SYSTEM_THREAD_INFORMATION)) return CallStatus::CensusFailed;
                const auto threads = reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(p + 1);
                for (size_t i = 0; i < p->NumberOfThreads; ++i) {
                    if (reinterpret_cast<uintptr_t>(threads[i].ClientId.UniqueProcess) != pid) return CallStatus::CensusFailed;
                    const auto id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(threads[i].ClientId.UniqueThread));
                    if (!id) return CallStatus::CensusFailed;
                    for (size_t j = 0; j < count; ++j) if (ids[j] == id) return CallStatus::CensusFailed;
                    ids[count++] = id;
                }
                return CallStatus::Installed; // Census success, no code changed.
            }
            if (!p->NextEntryOffset) break;
            offset += p->NextEntryOffset;
        }
        return CallStatus::CensusFailed;
    }
};
struct Resources {
    Census census;
    std::array<Peer, MaxThreads> peers{};
    size_t count = 0;
    // Called explicitly before any other cleanup, including handle closure.
    unsigned Resume() noexcept {
        unsigned failures = 0;
        for (size_t i = count; i; --i) {
            auto& p = peers[i - 1];
            if (!p.suspended) continue;
            if (ResumeThread(p.handle) == DWORD(-1) && WaitForSingleObject(p.handle, 0) != WAIT_OBJECT_0) ++failures;
            else p.suspended = false;
        }
        return failures;
    }
    ~Resources() {
        Resume();
        for (size_t i = 0; i < count; ++i) if (peers[i].handle) CloseHandle(peers[i].handle);
        if (census.buffer) VirtualFree(census.buffer, 0, MEM_RELEASE);
    }
};
bool ReadExecuteRegion(void* start, size_t size) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(start, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.Protect != PAGE_EXECUTE_READ) return false;
    const auto offset = reinterpret_cast<uintptr_t>(start) - reinterpret_cast<uintptr_t>(memory.BaseAddress);
    return offset <= memory.RegionSize && size <= memory.RegionSize - offset;
}
template<size_t N>
CallStatus FrozenInstall(Resources& resources, const std::array<CallSite, N>& sites,
                         const std::array<std::array<unsigned char, 5>, N>& replacements,
                         void* page, size_t pageBytes, DWORD pid, DWORD current, CallReport& report) noexcept {
    for (size_t i = 0; i < resources.count; ++i) {
        auto& peer = resources.peers[i];
        if (INJECT(Suspend) || SuspendThread(peer.handle) == DWORD(-1)) return CallStatus::SuspendFailed;
        peer.suspended = true; ++report.suspended;
        CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
        if (INJECT(Context) || !GetThreadContext(peer.handle, &context)) return CallStatus::ContextFailed;
        for (const auto& site : sites) {
            const auto start = reinterpret_cast<uintptr_t>(site.address);
            if (context.Eip >= start && context.Eip < start + site.expected.size()) return CallStatus::InstructionBusy;
        }
    }
    // Peers may have created a thread while handles were opened/suspension
    // progressed. Re-enumerate into the already allocated native-query buffer.
    const auto census = resources.census.Read(pid);
    if (census != CallStatus::Installed) return census;
    bool sawCurrent = false;
    for (size_t i = 0; i < resources.census.count; ++i) {
        const DWORD id = resources.census.ids[i];
        if (id == current) { sawCurrent = true; continue; }
        bool frozen = false;
        for (size_t j = 0; j < resources.count; ++j)
            if (resources.peers[j].id == id && resources.peers[j].suspended) frozen = true;
        if (!frozen) return CallStatus::ThreadsChanged;
    }
    if (!sawCurrent || INJECT(ThreadsChanged)) return CallStatus::ThreadsChanged;
    // A single instruction may straddle two pages. Require one uniform RX
    // region again while peers are frozen so one protection value covers both.
    if (!ReadExecuteRegion(page, pageBytes)) return CallStatus::UnsupportedProtection;
    for (const auto& site : sites)
        if (std::memcmp(site.address, site.expected.data(), site.expected.size())) return CallStatus::ByteMismatch;
    DWORD original = 0;
    if (!VirtualProtect(page, pageBytes, PAGE_EXECUTE_READWRITE, &original)) return CallStatus::ProtectFailed;
    report.protectionRestored = false;
    // The page was RX at preflight. Preserve an intervening owner's protection
    // instead of treating a changed state as authority to patch it.
    if (original != PAGE_EXECUTE_READ) {
        DWORD unused = 0;
        report.protectionRestored = VirtualProtect(page, pageBytes, original, &unused) != FALSE;
        return report.protectionRestored ? CallStatus::UnsupportedProtection : CallStatus::RestoreFailed;
    }
    std::memcpy(sites[0].address, replacements[0].data(), replacements[0].size());
    report.wrote = true;
    CallStatus failure = CallStatus::Installed;
    if (INJECT(AfterFirstWrite)) failure = CallStatus::WriteFailed;
    if (failure == CallStatus::Installed)
        for (size_t i = 1; i < N; ++i) std::memcpy(sites[i].address, replacements[i].data(), replacements[i].size());
    if (failure == CallStatus::Installed && (INJECT(Flush) || !FlushInstructionCache(GetCurrentProcess(), page, pageBytes))) failure = CallStatus::FlushFailed;
    DWORD unused = 0;
    if (failure == CallStatus::Installed && (INJECT(Restore) || !VirtualProtect(page, pageBytes, original, &unused))) failure = CallStatus::RestoreFailed;
    if (failure == CallStatus::Installed) {
        report.protectionRestored = true; report.installed = true;
        return CallStatus::Installed;
    }
    // Still writable: restore only the five bytes this transaction owned.
    for (const auto& site : sites) std::memcpy(site.address, site.expected.data(), 5);
    report.rolledBack = true;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), page, pageBytes) != FALSE;
    report.protectionRestored = VirtualProtect(page, pageBytes, original, &unused) != FALSE;
    return flushed && report.protectionRestored ? failure : CallStatus::RollbackFailed;
}
}
#ifdef VII_CODE_CALLS_TESTING
void SetCallFault(CallFault value) noexcept { fault = value; }
#endif
template<size_t N>
CallReport ReplaceCalls(const std::array<CallSite, N>& sites, bool entryJump = false) noexcept {
    CallReport report;
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const auto pageBytes = static_cast<uintptr_t>(system.dwPageSize);
    if (!pageBytes || (pageBytes & (pageBytes - 1))) return report;
    const auto first = reinterpret_cast<uintptr_t>(sites[0].address);
    const auto second = reinterpret_cast<uintptr_t>(sites[N - 1].address);
    const auto pageAddress = first & ~(pageBytes - 1);
    if (!first || !second || first > UINTPTR_MAX - 8 || second > UINTPTR_MAX - 8 ||
        (N > 1 && first < second + 8 && second < first + 8) || (second & ~(pageBytes - 1)) != pageAddress ||
        (N > 1 && (first + 8 > pageAddress + pageBytes || second + 8 > pageAddress + pageBytes))) return report;
    const auto protectBytes = N == 1 && first + 8 > pageAddress + pageBytes ? pageBytes * 2 : pageBytes;
    if (!ReadExecuteRegion(reinterpret_cast<void*>(pageAddress), protectBytes)) {
        report.status = CallStatus::UnsupportedProtection; return report;
    }
    std::array<std::array<unsigned char, 5>, N> replacements{};
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto& site = sites[i];
        if (!site.replacement || (entryJump && N != 1) || (!entryJump && site.expected[0] != 0xe8)) return report;
        if (N == 2 && (site.expected[5] != 0x83 || site.expected[6] != 0xc4 || site.expected[7] != 0x10)) return report;
        if (std::memcmp(site.address, site.expected.data(), site.expected.size())) {
            report.status = CallStatus::ByteMismatch; return report;
        }
        replacements[i][0] = entryJump ? 0xe9 : 0xe8;
        // Unsigned x86 address arithmetic intentionally wraps modulo 2^32.
        const auto relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site.replacement) - reinterpret_cast<uintptr_t>(site.address) - 5);
        std::memcpy(replacements[i].data() + 1, &relative, sizeof(relative));
    }
    Resources resources;
    resources.census.query = reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!resources.census.query) { report.status = CallStatus::QueryUnavailable; return report; }
    resources.census.buffer = VirtualAlloc(nullptr, CensusBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!resources.census.buffer) { report.status = CallStatus::AllocationFailed; return report; }
    const DWORD pid = GetCurrentProcessId(), current = GetCurrentThreadId();
    report.status = resources.census.Read(pid);
    if (report.status != CallStatus::Installed) return report;
    bool sawCurrent = false;
    for (size_t i = 0; i < resources.census.count; ++i) {
        const DWORD id = resources.census.ids[i];
        if (id == current) { sawCurrent = true; continue; }
        auto& peer = resources.peers[resources.count++]; peer.id = id;
        peer.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id);
        if (!peer.handle || GetThreadId(peer.handle) != id || GetProcessIdOfThread(peer.handle) != pid) {
            report.status = CallStatus::OpenThreadFailed; return report;
        }
    }
    if (!sawCurrent) { report.status = CallStatus::CensusFailed; return report; }
    report.peers = static_cast<unsigned>(resources.count);
    const auto before = GetTickCount64();
    report.status = FrozenInstall(resources, sites, replacements, reinterpret_cast<void*>(pageAddress), protectBytes, pid, current, report);
    report.resumeFailures = resources.Resume();
    report.frozenMilliseconds = GetTickCount64() - before;
    if (report.resumeFailures) report.status = CallStatus::ResumeFailed;
    return report;
}
CallReport ReplaceTwoCalls(const std::array<CallSite, 2>& sites) noexcept { return ReplaceCalls(sites); }
CallReport ReplaceCall(const CallSite& site) noexcept { return ReplaceCalls(std::array<CallSite, 1>{site}); }
CallReport ReplaceEntryJump(const CallSite& site) noexcept { return ReplaceCalls(std::array<CallSite, 1>{site}, true); }
const char* CallStatusName(CallStatus status) noexcept {
    switch (status) {
#define NAME(value) case CallStatus::value: return #value
        NAME(Installed); NAME(InvalidSites); NAME(UnsupportedProtection); NAME(ByteMismatch);
        NAME(QueryUnavailable); NAME(AllocationFailed); NAME(CensusFailed); NAME(ThreadLimit);
        NAME(OpenThreadFailed); NAME(SuspendFailed); NAME(ContextFailed); NAME(InstructionBusy);
        NAME(ThreadsChanged); NAME(ProtectFailed); NAME(WriteFailed); NAME(FlushFailed);
        NAME(RestoreFailed); NAME(RollbackFailed); NAME(ResumeFailed);
#undef NAME
    }
    return "Unknown";
}
}
