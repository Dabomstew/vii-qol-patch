#include "patch.hpp"
#include "code_calls.hpp"
#include "new_game_detector.hpp"
#include "load_timing.hpp"
#include <cstring>

namespace vii {
namespace {
static_assert(sizeof(void*) == 4, "Only the verified x86 game build is supported");
constexpr uintptr_t TransitionRva = 0x19d380;
constexpr size_t RelocatedEntryBytes = 8; // push ebp; mov ebp,esp; call rel32

// The marker and fields live in the proxy image, so ASL can locate this state
// without a game-heap root. The game pointer is retained only for optional
// diagnostics/guards; sequence is the trigger signal.
volatile NewGameBridgeState bridge{
    NewGameBridgeMagic0, NewGameBridgeMagic1, 0, 0
};
uintptr_t transitionTrampoline = 0;

extern "C" __declspec(noinline) void __cdecl PublishNewGameTransition(void* transition) noexcept {
    if (!transition) return;
    bridge.transition = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transition));
    const auto sequence = InterlockedIncrement(reinterpret_cast<volatile LONG*>(&bridge.sequence));
    NotifyLoadTimingNewGame(static_cast<uint32_t>(sequence));
}

// The entry jump replaces the first five bytes, which cuts through the
// original rel32 CALL. Keep the complete eight-byte instruction prefix in the
// trampoline and rewrite that CALL relative to the trampoline location.
extern "C" __declspec(naked) void __cdecl NewGameTransitionHook() {
    __asm {
        pushfd
        pushad
        // At this point the original fifth argument [entry ESP+14h] is
        // [ESP+38h]: pushfd (4) + pushad (32) precede the original stack.
        push dword ptr [esp + 38h]
        call PublishNewGameTransition
        add esp, 4
        popad
        popfd
        mov eax, dword ptr [transitionTrampoline]
        jmp eax
    }
}

bool BuildTransitionTrampoline(uintptr_t entry) noexcept {
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline) return false;

    std::memcpy(trampoline, reinterpret_cast<const void*>(entry), 3);
    trampoline[3] = 0xe8;
    int32_t originalRelative = 0;
    std::memcpy(&originalRelative, reinterpret_cast<const void*>(entry + 4), sizeof(originalRelative));
    const auto callTarget = entry + RelocatedEntryBytes + static_cast<intptr_t>(originalRelative);
    const auto relocatedRelative = static_cast<uint32_t>(callTarget - reinterpret_cast<uintptr_t>(trampoline + 8));
    std::memcpy(trampoline + 4, &relocatedRelative, sizeof(relocatedRelative));

    trampoline[8] = 0xe9;
    const auto resumeRelative = static_cast<uint32_t>((entry + RelocatedEntryBytes) -
        reinterpret_cast<uintptr_t>(trampoline + 13));
    std::memcpy(trampoline + 9, &resumeRelative, sizeof(resumeRelative));

    DWORD old = 0;
    if (!VirtualProtect(trampoline, 4096, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), trampoline, 13)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    transitionTrampoline = reinterpret_cast<uintptr_t>(trampoline);
    return true;
}
}

bool InstallNewGameDetector(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    auto* entry = reinterpret_cast<unsigned char*>(base + TransitionRva);
    const std::array<unsigned char, 8> expected{
        0x55, 0x8b, 0xec, 0xe8, 0xe8, 0x27, 0x20, 0x00
    };
    if (std::memcmp(entry, expected.data(), expected.size())) {
        Log("NewGameDetector transition signature mismatch; preserving game");
        return false;
    }
    if (!BuildTransitionTrampoline(reinterpret_cast<uintptr_t>(entry))) {
        Log("NewGameDetector trampoline allocation/protection failed");
        return false;
    }

    CallSite site{entry, expected, reinterpret_cast<void*>(NewGameTransitionHook)};
    const auto report = ReplaceEntryJump(site);
    const bool installed = report.installed && report.status == CallStatus::Installed &&
        report.protectionRestored && !report.resumeFailures;
    Log("NewGameDetector hook=%s installed=%d rollback=%d peers=%u resume_failures=%u",
        CallStatusName(report.status), installed, report.rolledBack, report.peers, report.resumeFailures);
    if (!installed) {
        // Keep the trampoline resident if the transaction was uncertain; the
        // proxy is pinned and a later cleanup must never free code still in use.
        return false;
    }
    return true;
}
}
