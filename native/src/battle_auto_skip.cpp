#include "patch.hpp"
#include "code_calls.hpp"
#include "battle_auto_skip.hpp"
#include <cstring>

namespace vii {
namespace {
BattleSkipAdapter adapter;
BattleSkipAdapter resultAdapter;
BattleSkipAdapter defeatAdapter;
BattleSkipAdapter movementAdapter;
BattleSkipAdapter cutinAdapter;
int __fastcall PollBattleSkip(void* input, void*, unsigned mask) {
    return adapter.Poll(input, mask);
}
int __fastcall PollResultSkip(void* input, void*, unsigned mask) {
    return resultAdapter.Poll(input, mask);
}
int __fastcall PollDefeatSkip(void* input, void*, unsigned mask) {
    return defeatAdapter.Poll(input, mask);
}
int __fastcall PollMovementSkip(void* input, void*, unsigned mask) {
    return movementAdapter.Poll(input, mask);
}
int __fastcall PollCutinSkip(void* input, void*, unsigned mask) {
    return cutinAdapter.Poll(input, mask);
}
}

int BattleSkipAdapter::Poll(void* input, unsigned mask) {
    const int physical = original(input, mask);
    if (!enabled.load(std::memory_order_acquire) || mask != 1) return physical;
    const auto count = forced.fetch_add(1, std::memory_order_relaxed) + 1;
    if (trace && count <= 128)
        Log("BattleAutoSkip kind=%s query=%u physical=%d effective=1", kind, count, physical);
    return 1;
}

bool InstallBattleAutoSkip(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    const unsigned char poll[] = {0x55,0x8b,0xec,0x8b,0x41,0x18,0x23,0x45,0x08,0x5d,0xc2,0x04,0x00};
    unsigned char edge[] = {0x55,0x8b,0xec,0x8b,0x55,0x08,0xa1,0,0,0,0,0x3b,0xd0,0x75,0x09,
        0x80,0x3d,0,0,0,0,0x00,0x75,0x05,0x8b,0x41,0x1c,0x23,0xc2,0x5d,0xc2,0x04,0x00};
    const auto confirm = base+0x47c9a44, overrideFlag = base+0x6efdb0;
    std::memcpy(edge+7,&confirm,4); std::memcpy(edge+17,&overrideFlag,4);
    const CallSite sites[] = {
        {reinterpret_cast<unsigned char*>(base+0x25dffd), {0xe8,0x4e,0x40,0x14,0x00,0x85,0xc0,0x74}, reinterpret_cast<void*>(PollBattleSkip)},
        {reinterpret_cast<unsigned char*>(base+0x25e166), {0xe8,0xe5,0x3e,0x14,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollBattleSkip)},
        {reinterpret_cast<unsigned char*>(base+0x25e4a5), {0xe8,0xa6,0x3b,0x14,0x00,0x85,0xc0,0x74}, reinterpret_cast<void*>(PollBattleSkip)},
        {reinterpret_cast<unsigned char*>(base+0x25e62e), {0xe8,0x1d,0x3a,0x14,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollBattleSkip)},
        {reinterpret_cast<unsigned char*>(base+0x266f70), {0xe8,0xeb,0xb0,0x13,0x00,0x85,0xc0,0x74}, reinterpret_cast<void*>(PollResultSkip)},
        // Entity state7: original eligible-LT actor/voice cleanup after defeat.
        {reinterpret_cast<unsigned char*>(base+0x253beb), {0xe8,0x60,0xe4,0x14,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollDefeatSkip)},
        // Enemy route movement and its original turn-state continuation.
        {reinterpret_cast<unsigned char*>(base+0x27d454), {0xe8,0xf7,0x4b,0x12,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollMovementSkip)},
        {reinterpret_cast<unsigned char*>(base+0x271234), {0xe8,0x17,0x0e,0x13,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollMovementSkip)},
        // Original immediate scheduler path omits multi-participant cut-in setup.
        {reinterpret_cast<unsigned char*>(base+0x27f9d7), {0xe8,0x74,0x26,0x12,0x00,0x85,0xc0,0x0f}, reinterpret_cast<void*>(PollCutinSkip)},
    };
    if (std::memcmp(reinterpret_cast<void*>(base+0x3a2050), poll, sizeof(poll)) ||
        std::memcmp(reinterpret_cast<void*>(base+0x3a2060), edge, sizeof(edge))) {
        Log("BattleAutoSkip original query mismatch; preserving existing hooks"); return false;
    }
    for (const auto& site : sites) {
        if (std::memcmp(site.address, site.expected.data(), site.expected.size())) {
            Log("BattleAutoSkip call mismatch; preserving existing hooks"); return false;
        }
    }
    adapter.original = reinterpret_cast<BattleInputPoll>(base+0x3a2050);
    adapter.trace = Option(context, L"BattleAutoSkip", L"Trace", 0) != 0;
    resultAdapter.original = reinterpret_cast<BattleInputPoll>(base+0x3a2060);
    resultAdapter.trace = adapter.trace;
    resultAdapter.kind = "result";
    defeatAdapter.original = adapter.original;
    defeatAdapter.trace = adapter.trace;
    defeatAdapter.kind = "defeat";
    movementAdapter.original = adapter.original;
    movementAdapter.trace = adapter.trace;
    movementAdapter.kind = "movement";
    cutinAdapter.original = adapter.original;
    cutinAdapter.trace = adapter.trace;
    cutinAdapter.kind = "cutin";
    // Publish behavior only after every guarded call is installed. If a later
    // transaction fails, earlier resident adapters keep original input behavior.
    // The proxy is pinned before installers run; no adapter is hot-unloaded.
    for (const auto& site : sites) {
        const auto report = ReplaceCall(site);
        Log("BattleAutoSkip call=%08x hook=%s installed=%d rollback=%d resume_failures=%u",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(site.address)-base),
            CallStatusName(report.status), report.installed, report.rolledBack, report.resumeFailures);
        if (!report.installed || report.status != CallStatus::Installed ||
            !report.protectionRestored || report.resumeFailures) return false;
    }
    adapter.enabled.store(true, std::memory_order_release);
    resultAdapter.enabled.store(true, std::memory_order_release);
    defeatAdapter.enabled.store(true, std::memory_order_release);
    movementAdapter.enabled.store(true, std::memory_order_release);
    cutinAdapter.enabled.store(true, std::memory_order_release);
    return true;
}
}
