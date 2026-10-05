#include "motion_adapter.hpp"
#include "code_calls.hpp"
#include <intrin.h>
#include <cstring>
#include <memory>

namespace vii {
namespace {
std::atomic<MotionAdapter*> adapter{nullptr};
int __cdecl Get(uint32_t nameSpace, const char* path, void* bytes, uint32_t size) {
    return adapter.load()->Get(reinterpret_cast<uintptr_t>(_ReturnAddress()), nameSpace, path, bytes, size);
}
int __cdecl Put(uint32_t nameSpace, const char* path, void* bytes, uint32_t size) {
    return adapter.load()->Put(reinterpret_cast<uintptr_t>(_ReturnAddress()), nameSpace, path, bytes, size);
}
bool OriginalEntries(uintptr_t base) {
    unsigned char signature[] = {0x55,0x8b,0xec,0xa1,0,0,0,0,0x85,0xc0,0x75,0x04,0x33,0xc0,0x5d,0xc3};
    const auto manager = base + 0x047cf438;
    std::memcpy(signature + 4, &manager, 4);
    return !std::memcmp(reinterpret_cast<void*>(base + 0x003fa2a0), signature, sizeof(signature)) &&
        !std::memcmp(reinterpret_cast<void*>(base + 0x003fa1f0), signature, sizeof(signature));
}
}
bool InstallMotionCache(const Context& context) {
    const int mib = Option(context, L"MotionCache", L"BudgetMiB", 16);
    wchar_t mode[16]{};
    GetPrivateProfileStringW(L"MotionCache", L"Mode", L"Off", mode, 16, context.ini.c_str());
    const bool audit = lstrcmpiW(mode, L"Audit") == 0, replay = lstrcmpiW(mode, L"Replay") == 0;
    if ((!audit && !replay) || mib < 1 || mib > 64) { Log("MotionCache invalid mode/budget; disabled"); return false; }
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    if (!OriginalEntries(base)) { Log("MotionCache original target mismatch; preserving existing hooks"); return false; }
    auto candidate = std::make_unique<MotionAdapter>(audit ? MotionBytes::Mode::Audit : MotionBytes::Mode::Replay,
        static_cast<size_t>(mib)*1024*1024, reinterpret_cast<MotionFileFn>(base + 0x003fa2a0),
        reinterpret_cast<MotionFileFn>(base + 0x003fa1f0), base + 0x0039c421, base + 0x0039c6dd,
        reinterpret_cast<const uintptr_t*>(base + 0x047cf438));
    const std::array<CallSite, 2> sites = {{
        {reinterpret_cast<unsigned char*>(base + 0x0039c41c), {0xe8,0x7f,0xde,0x05,0x00,0x83,0xc4,0x10}, reinterpret_cast<void*>(Get)},
        {reinterpret_cast<unsigned char*>(base + 0x0039c6d8), {0xe8,0x13,0xdb,0x05,0x00,0x83,0xc4,0x10}, reinterpret_cast<void*>(Put)}
    }};
    // Published before reachable code. Pinned module and state live until exit,
    // including failed transactions; disabled adapters always call originals.
    auto state = candidate.release(); adapter.store(state);
    const auto report = ReplaceTwoCalls(sites);
    const bool ready = report.installed && report.status == CallStatus::Installed && report.protectionRestored && !report.resumeFailures;
    if (ready) state->Enable();
    Log("MotionCache transaction=%s peers=%u suspended=%u wrote=%d installed=%d rollback=%d protection_restored=%d resume_failures=%u frozen_ms=%llu",
        CallStatusName(report.status), report.peers, report.suspended, report.wrote, report.installed,
        report.rolledBack, report.protectionRestored, report.resumeFailures, report.frozenMilliseconds);
    Log("MotionCache mode=%ls budget_mib=%d enabled=%d", mode, mib, ready);
    return ready;
}
bool ReadMotionTelemetry(MotionTelemetry& output) {
    auto state = adapter.load();
    if (!state) return false;
    output = state->Snapshot(); return true;
}
}
extern "C" int __cdecl VIIGetMotionCacheStats(vii::MotionTelemetry* output, uint32_t size) {
    if (!output || size != sizeof(*output)) return 0;
    try { return vii::ReadMotionTelemetry(*output) ? 1 : 0; } catch (...) { return 0; }
}
