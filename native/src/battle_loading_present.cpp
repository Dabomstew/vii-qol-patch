#include "battle_loading_present.hpp"
#include "code_calls.hpp"
#include <atomic>
#include <cstring>

namespace vii {
namespace {
static_assert(sizeof(void*) == 4, "Verified x86 callsite only");
std::atomic<bool> enabled{false};
SRWLOCK lock = SRWLOCK_INIT;
void** rendererRoot = nullptr;
void* presentResume = nullptr;
bool trace = false;
struct Lifecycle {
    void* task = nullptr;
    void* object = nullptr;
    IDXGISwapChain* chain = nullptr; // Borrowed; the live renderer must still own it.
    DWORD thread = 0;
    uint32_t state = 0, polls = 0, skipped = 0, real = 0;
    uint64_t generation = 0, began = 0, lastReal = 0;
    bool checked = false;
} life;
#ifdef VII_BATTLE_PRESENT_TESTING
uint64_t testNow = 0;
bool testFocus = true;
#endif
uint64_t Now() noexcept {
#ifdef VII_BATTLE_PRESENT_TESTING
    return testNow;
#else
    return GetTickCount64();
#endif
}
bool Foreground() noexcept {
#ifdef VII_BATTLE_PRESENT_TESTING
    return testFocus;
#else
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
#endif
}
void Reset(const char* reason) noexcept {
    if (trace && life.task)
        Log("SkipBattleLoadingPresent end=%s skipped=%u real=%u state=%u", reason, life.skipped, life.real, life.state);
    const auto generation = life.generation + 1;
    const auto polls = life.polls;
    life = {};
    life.generation = generation;
    life.polls = polls;
}
bool ReadOwner(void* task, void*& object, uint32_t& state) noexcept {
    __try {
        object = task ? *reinterpret_cast<void**>(static_cast<unsigned char*>(task) + 0x24) : nullptr;
        state = object ? *reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(object) + 4) : UINT32_MAX;
        return object != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadRenderer(IDXGISwapChain* chain) noexcept {
    __try {
        auto* renderer = rendererRoot ? static_cast<unsigned char*>(*rendererRoot) : nullptr;
        return renderer && *reinterpret_cast<IDXGISwapChain**>(renderer + 0x34) == chain &&
            *reinterpret_cast<uint32_t*>(renderer + 0x7c) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool DiscardChain(IDXGISwapChain* chain) noexcept {
    __try {
        DXGI_SWAP_CHAIN_DESC desc{};
        return chain && chain->GetDesc(&desc) == S_OK && desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Suppress(IDXGISwapChain* chain, UINT sync, UINT flags, uint64_t& generation) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return false;
    AcquireSRWLockExclusive(&lock);
    bool suppress = false;
    if (life.task && !life.polls) {
        void* object = nullptr; uint32_t state = 0;
        const auto now = Now();
        if (GetCurrentThreadId() != life.thread || !Foreground() || sync != 0 || flags != 0 ||
            now < life.began || now - life.began >= 15000 || life.skipped >= 20000 ||
            !ReadOwner(life.task, object, state) || object != life.object || state != life.state ||
            state < 1 || state > 3 || !ReadRenderer(chain)) {
            Reset("guard");
        } else if ((!life.chain && !DiscardChain(chain)) || (life.chain && life.chain != chain)) {
            Reset("chain");
        } else {
            life.chain = chain;
            generation = life.generation;
            // First and periodic real calls expose occlusion/device errors and
            // keep the window responsive. No DXGI_TEST call is intercepted.
            if (life.checked && now >= life.lastReal && now - life.lastReal < 250) {
                ++life.skipped;
                suppress = true;
            } else {
                life.lastReal = now;
                life.checked = true;
                if (trace) ++life.real;
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return suppress;
}
HRESULT __stdcall Present(IDXGISwapChain* chain, UINT sync, UINT flags) {
    uint64_t generation = UINT64_MAX;
    if (Suppress(chain, sync, flags, generation)) return S_OK;
    // Dynamic COM dispatch retains overlay/runtime ownership and exact HRESULT.
    // Original exceptions propagate; never turn a native failure into success.
    HRESULT result = E_FAIL;
    bool returned = false;
    __try { result = chain->Present(sync, flags); returned = true; }
    __finally {
        if ((!returned || result != S_OK) && enabled.load(std::memory_order_acquire)) {
            AcquireSRWLockExclusive(&lock);
            if (generation == life.generation) Reset("present-result");
            ReleaseSRWLockExclusive(&lock);
        }
    }
    return result;
}
// Entered with the original three stdcall arguments already on the stack.
// Reproduce every overwritten instruction; resume at the original JNE.
__declspec(naked) void PresentEntry() {
    __asm {
        call Present
        pop esi
        cmp eax, 087a0001h
        jmp dword ptr [presentResume]
    }
}
}

BattlePresentPoll BeginBattleLoadingPresent(void* task, uint32_t mode) noexcept {
    BattlePresentPoll token;
    if (!enabled.load(std::memory_order_acquire)) return token;
    token.observed = true; token.task = task; token.thread = GetCurrentThreadId();
    const bool readable = mode == 2 && ReadOwner(task, token.object, token.state);
    AcquireSRWLockExclusive(&lock);
    const bool nested = life.polls != 0;
    if (nested || mode != 2 || !readable) Reset("callback");
    else if (life.task && (life.task != task || life.object != token.object ||
        life.thread != token.thread || life.state != token.state)) Reset("owner");
    ++life.polls;
    token.generation = life.generation;
    token.valid = readable && !nested;
    ReleaseSRWLockExclusive(&lock);
    return token;
}
void EndBattleLoadingPresent(const BattlePresentPoll& token, bool returned) noexcept {
    if (!token.observed) return;
    void* object = nullptr; uint32_t state = 0;
    const bool readable = token.valid && returned && ReadOwner(token.task, object, state);
    AcquireSRWLockExclusive(&lock);
    if (life.polls) --life.polls;
    if (life.polls || !readable || token.generation != life.generation || object != token.object) {
        Reset("poll-result");
    } else if (token.state == 0 && state == 1) {
        Reset("new");
        life.task = token.task; life.object = object; life.thread = token.thread;
        life.state = state; life.began = Now();
    } else if (life.task == token.task && life.object == object && life.thread == token.thread &&
               life.state == token.state && state >= 1 && state <= 3 &&
               (state == token.state || state == token.state + 1)) {
        life.state = state;
    } else {
        Reset(state == 4 ? "complete" : "state");
    }
    ReleaseSRWLockExclusive(&lock);
}
bool InstallSkipBattleLoadingPresent(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    auto* site = reinterpret_cast<unsigned char*>(base + 0x39354d);
    // CALL [ECX+20]; POP ESI; CMP EAX,DXGI_STATUS_OCCLUDED; JNE.
    const unsigned char span[]{0xff,0x51,0x20,0x5e,0x3d,0x01,0x00,0x7a,0x08,0x75,0x0c};
    if (std::memcmp(site, span, sizeof(span))) {
        Log("SkipBattleLoadingPresent callsite unavailable"); return false;
    }
    trace = Option(context, L"SkipBattleLoadingPresent", L"Trace", 0) != 0;
    rendererRoot = reinterpret_cast<void**>(base + 0x47c6464);
    presentResume = site + 9;
    std::array<unsigned char,8> expected{};
    std::memcpy(expected.data(), span, expected.size());
    const auto report = ReplaceEntryJump({site, expected, reinterpret_cast<void*>(PresentEntry)});
    Log("SkipBattleLoadingPresent hook=%s installed=%d rollback=%d resume_failures=%u",
        CallStatusName(report.status), report.installed, report.rolledBack, report.resumeFailures);
    if (!report.installed || report.status != CallStatus::Installed ||
        !report.protectionRestored || report.resumeFailures) return false;
    enabled.store(true, std::memory_order_release);
    return true;
}
#ifdef VII_BATTLE_PRESENT_TESTING
void ResetBattlePresentTest(void** renderer, bool active, bool diagnostic) {
    enabled.store(false); life = {}; rendererRoot = renderer; trace = diagnostic;
    enabled.store(active);
}
void SetBattlePresentClockTest(uint64_t now, bool focused) { testNow = now; testFocus = focused; }
HRESULT BattlePresentTest(IDXGISwapChain* chain, UINT sync, UINT flags) { return Present(chain, sync, flags); }
void* BattlePresentEntryTest() { return reinterpret_cast<void*>(PresentEntry); }
void SetBattlePresentResumeTest(void* resume) { presentResume = resume; }
#endif
}
