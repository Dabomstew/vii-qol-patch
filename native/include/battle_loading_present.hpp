#pragma once
#include "patch.hpp"
#include <dxgi.h>

namespace vii {
// Stack-owned token. No owner pointer is followed outside a guarded read.
struct BattlePresentPoll {
    void* task = nullptr;
    void* object = nullptr;
    uint64_t generation = 0;
    uint32_t state = 0;
    DWORD thread = 0;
    bool observed = false, valid = false;
};
BattlePresentPoll BeginBattleLoadingPresent(void* task, uint32_t mode) noexcept;
void EndBattleLoadingPresent(const BattlePresentPoll&, bool returned) noexcept;
bool InstallSkipBattleLoadingPresent(const Context&);
#ifdef VII_BATTLE_PRESENT_TESTING
void ResetBattlePresentTest(void** renderer, bool enabled, bool trace = false);
void SetBattlePresentClockTest(uint64_t milliseconds, bool focused);
HRESULT BattlePresentTest(IDXGISwapChain*, UINT, UINT);
void* BattlePresentEntryTest();
void SetBattlePresentResumeTest(void*);
#endif
}
