#pragma once
#include "patch.hpp"
#include "load_timing_core.hpp"
#include <cstddef>

namespace vii {
// Public x86 memory ABI. The descriptor's state pointer is resident for the
// process lifetime. Read sequence/payload/sequence; accept equal even values.
struct LoadTimingPayload {
    uint32_t status, coverage, reason, activeOwners;              // state +08..+14
    uint32_t generation, transitions, focused, fault;            // state +18..+24
    uint32_t fpsRequested, fpsGlobal, fpsApplications, reserved; // state +28..+34
    uint64_t frequency, completedTicks, openSinceQpc, lastQpc;    // +38..+50
    uint32_t newGameSequence, reserved2;                         // +58..+5c
    uint64_t newGameQpc, newGameExcludedTicks;                    // +60..+68
};
struct alignas(8) LoadTimingState {
    volatile LONG sequence = 0;
    uint32_t padding = 0;
    LoadTimingPayload data{};
};
struct LoadTimingDescriptor {
    uint32_t magic0, magic1, version, bytes;
    LoadTimingState* state;
    uint32_t stateBytes;
};
constexpr uint32_t LoadMagic0 = 0x4c494956; // VIIL
constexpr uint32_t LoadMagic1 = 0x31303054; // T001
static_assert(sizeof(LoadTimingPayload) == 104);
static_assert(offsetof(LoadTimingState, data) == 8);
static_assert(offsetof(LoadTimingPayload, frequency) == 48);
static_assert(offsetof(LoadTimingPayload, newGameQpc) == 88);
bool InstallLoadTiming(const Context&);
void NotifyLoadTimingNewGame(uint32_t sequence) noexcept;
#ifdef VII_LOAD_TIMING_TESTING
void ResetLoadTimingTest(uint32_t(__cdecl*)(uint32_t,uint32_t,void*), void(__fastcall*)(void*), bool, bool);
uint32_t LoadBattleTest(uint32_t, uint32_t, void*);
void LoadClockTest(void*);
void SetDungeonTimingTest(void(__cdecl*)(void*), void(__cdecl*)(void*), uint32_t(__cdecl*)());
void LoadDungeonTest(void*);
void DestroyDungeonTest(void*);
uint32_t LoadSetupTest();
void SetAdvTimingTest(void*(__thiscall*)(void*,void*), uint32_t(__fastcall*)(void*), uint32_t(__cdecl*)());
void* LoadAdvDataTest(void*, void*);
uint32_t LoadAdvScriptTest(void*);
uint32_t LoadAdvCancelTest();
void SetAdvQueueTimingTest(uint32_t(__cdecl*)(uint32_t));
uint32_t LoadAdvQueueTest(uint32_t);
void SetWorldTimingTest(void*(__thiscall*)(void*,void*), uint32_t(__cdecl*)(),
                       uint32_t(__cdecl*)(void*), uint32_t(__cdecl*)(void*),
                       uint32_t(__cdecl*)(void*,uint32_t), void**, void*);
void* LoadWorldDataTest(void*, void*);
uint32_t LoadWorldReadyTest();
uint32_t LoadWorldMapTest(void*);
uint32_t LoadWorldCharacterTest(void*);
uint32_t LoadWorldBusyTest(void*, uint32_t);
void SetTitleTimingTest(uint32_t(__cdecl*)(void*), void**, void*);
uint32_t LoadTitleSetupTest(void*);
void SetSaveTimingTest(void(__cdecl*)(),uint32_t(__cdecl*)(uint32_t),
                       uint32_t(__cdecl*)(uint32_t,uint32_t,uint32_t),
                       uint32_t(__cdecl*)(uint32_t,uint32_t,uint32_t),uint32_t(__cdecl*)(uint32_t),
                       uint32_t*,uint32_t*,uint32_t*,void**);
void LoadSaveScanTest();
uint32_t LoadSaveExistsTest(uint32_t);
uint32_t LoadSaveOpenTest(uint32_t,uint32_t,uint32_t);
uint32_t LoadSaveReadTest(uint32_t,uint32_t,uint32_t);
uint32_t LoadSaveCloseTest(uint32_t);
LoadTimingPayload ReadLoadTimingTest();
#endif
}
