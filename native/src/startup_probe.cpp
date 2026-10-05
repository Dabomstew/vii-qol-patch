#include "patch.hpp"
#include <tlhelp32.h>

namespace vii {
namespace {
bool ReadPointer(uintptr_t address, uintptr_t& value) noexcept {
    __try { value = *reinterpret_cast<const uintptr_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { value = 0; return false; }
}
}
void ProbeStartup(const Context& context) noexcept {
    if (!Option(context, L"Diagnostics", L"Startup", 0)) return;
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    uintptr_t manager = 0, cache = 0, head = 0, gate = 0;
    const bool managerRead = ReadPointer(base + 0x047c95f8, manager);
    const bool cacheRead = ReadPointer(base + 0x047cf438, cache);
    const bool headRead = manager && ReadPointer(manager + 0x5fef8, head);
    const bool gateRead = manager && ReadPointer(manager + 0x5ff00, gate);
    Log("StartupProbe pid=%lu tid=%lu base=%08x manager_read=%d manager=%08x cache_read=%d cache=%08x head_read=%d head=%08x gate_read=%d gate=%u",
        GetCurrentProcessId(), GetCurrentThreadId(), static_cast<unsigned>(base), managerRead,
        static_cast<unsigned>(manager), cacheRead, static_cast<unsigned>(cache), headRead,
        static_cast<unsigned>(head), gateRead, static_cast<unsigned>(gate));
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { Log("StartupProbe thread_snapshot_failed error=%lu", GetLastError()); return; }
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    unsigned count = 0;
    BOOL next = Thread32First(snapshot, &entry);
    while (next) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) {
            ++count;
            if (count <= 128) Log("StartupProbe thread=%lu current=%d", entry.th32ThreadID, entry.th32ThreadID == GetCurrentThreadId());
        }
        next = Thread32Next(snapshot, &entry);
    }
    const auto error = GetLastError();
    CloseHandle(snapshot);
    Log("StartupProbe threads=%u enumeration_complete=%d enumeration_error=%lu", count, error == ERROR_NO_MORE_FILES, error);
}
}
