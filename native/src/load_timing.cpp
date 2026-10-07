#include "load_timing.hpp"
#include "code_calls.hpp"
#include <atomic>
#include <cstring>

namespace vii {
namespace {
static_assert(sizeof(void*) == 4, "Verified x86 build only");
static_assert(sizeof(LoadTimingDescriptor) == 24);
static_assert(sizeof(LoadTimingState) == 112);
LoadTimingState bridge;
volatile LoadTimingDescriptor descriptor{LoadMagic0, LoadMagic1, 1, sizeof(LoadTimingDescriptor), &bridge, sizeof(bridge)};
SRWLOCK stateLock = SRWLOCK_INIT;
LoadTimingCore timing;
LoadTimingPayload payload{};
std::atomic<bool> enabled{false};
bool requestedFps = false, globalFps = false;
bool trace = false;
uint64_t frequency = 0;
using BattleCallback = uint32_t(__cdecl*)(uint32_t, uint32_t, void*);
using FrameClock = void(__fastcall*)(void*);
using DungeonCallback = void(__cdecl*)(void*);
using SetupPredicate = uint32_t(__cdecl*)();
BattleCallback originalBattle = nullptr;
FrameClock originalClock = nullptr;
DungeonCallback originalDungeon = nullptr, originalDestroy = nullptr;
SetupPredicate originalSetup = nullptr;
struct DungeonScope { void* core; uint32_t player = 0, calls = 0; bool waiting = false, ambiguous = false; };
thread_local DungeonScope* dungeonScope = nullptr;
using TaskData = void*(__thiscall*)(void*, void*);
using ScriptBusy = uint32_t(__fastcall*)(void*);
TaskData originalAdvData = nullptr;
ScriptBusy originalScriptBusy = nullptr;
SetupPredicate originalAdvCancel = nullptr;
using QueueBusy = uint32_t(__cdecl*)(uint32_t);
QueueBusy originalAdvQueue = nullptr;
struct AdvScope { void* task = nullptr; void* object = nullptr; uint32_t scene = 0, calls = 0, phase = 0; bool waiting = false; };
thread_local AdvScope advScope;
using ResourcePredicate = uint32_t(__cdecl*)(void*);
using CharacterBusy = uint32_t(__cdecl*)(void*, uint32_t);
TaskData originalWorldData = nullptr;
SetupPredicate originalWorldReady = nullptr;
ResourcePredicate originalWorldMap = nullptr, originalWorldCharacter = nullptr;
CharacterBusy originalCharacterBusy = nullptr;
void* worldMainCallback = nullptr;
void** worldRoot = nullptr;
struct WorldIdentity { void* task = nullptr; void* object = nullptr; };
struct WorldProof { uint32_t mapCalls = 0, characterCalls = 0; bool mapBusy = false, characterBusy = false, ambiguous = false; };
struct CharacterProof { bool busy = false; };
thread_local WorldIdentity worldIdentity;
thread_local WorldProof* worldProof = nullptr;
thread_local CharacterProof* characterProof = nullptr;

ResourcePredicate originalTitleSetup = nullptr;
void** titleRoot = nullptr;
void* titleMainCallback = nullptr;
thread_local bool inTitleSetup = false;

using SaveScan = void(__cdecl*)();
using SaveOne = uint32_t(__cdecl*)(uint32_t);
using SaveThree = uint32_t(__cdecl*)(uint32_t,uint32_t,uint32_t);
SaveScan originalSaveScan = nullptr;
SaveOne originalSaveExists = nullptr, originalSaveClose = nullptr;
SaveThree originalSaveOpen = nullptr, originalSaveRead = nullptr;
uint32_t *saveMode = nullptr, *saveState = nullptr, *savePhase = nullptr;
void** saveList = nullptr;
struct SaveScope { bool eligible = false, ioActive = false; };
thread_local SaveScope* saveScope = nullptr;

uint64_t Now() noexcept { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return static_cast<uint64_t>(q.QuadPart); }
void Publish() noexcept {
    const auto& s = timing.Sample();
    payload.status = static_cast<uint32_t>(s.status);
    payload.coverage = LoadCoverageKnown; payload.reason = s.reason;
    payload.activeOwners = s.owners; payload.generation = s.generation;
    payload.transitions = s.transitions; payload.focused = s.focused ? 1 : 0;
    payload.fault = static_cast<uint32_t>(s.fault);
    payload.fpsRequested = requestedFps ? 1 : 0; payload.fpsGlobal = globalFps ? 1 : 0;
    payload.frequency = frequency; payload.completedTicks = s.completed;
    payload.openSinceQpc = s.opened; payload.lastQpc = s.lastQpc;
    InterlockedIncrement(&bridge.sequence);
    MemoryBarrier();
    std::memcpy(&bridge.data, &payload, sizeof(payload));
    MemoryBarrier();
    InterlockedIncrement(&bridge.sequence);
}
// Restrict guarded reads to the live callback. Never dereference a retained
// owner pointer from the pacing hook or an external-reader thread.
bool ReadPhase(void* task, uint32_t& object, uint32_t& phase) noexcept {
    __try {
        object = task ? *reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(task) + 0x24) : 0;
        phase = object ? *reinterpret_cast<uint32_t*>(object + 4) : UINT32_MAX;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Observe(void* task, bool cancel, uint32_t expectedObject = 0) noexcept {
    uint32_t object = 0, phase = 0;
    const bool readable = cancel || ReadPhase(task, object, phase);
    AcquireSRWLockExclusive(&stateLock);
    const auto before = timing.Sample().transitions;
    const auto q = Now();
    if (!readable) timing.Fail(LoadFault::Memory, timing.Sample().lastQpc);
    else if (cancel) timing.Cancel(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(task)), q);
    else timing.Observe(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(task)), object,
        expectedObject != 0 && object == expectedObject && phase == 2, q);
    Publish();
    const auto sample = timing.Sample();
    ReleaseSRWLockExclusive(&stateLock);
    if (trace && before != sample.transitions)
        Log("LoadTiming transition=%u reason=%u completed=%llu open=%llu qpc=%llu phase=%u",
            sample.transitions, sample.reason, sample.completed, sample.opened, q, phase);
}
uint32_t __cdecl BattleHook(uint32_t command, uint32_t mode, void* task) {
    if (!enabled.load(std::memory_order_acquire)) return originalBattle(command, mode, task);
    uint32_t beforeObject = 0, beforePhase = 0;
    if (mode == 0 || mode == 1) Observe(task, true);
    else if (mode == 2) {
        if (!ReadPhase(task, beforeObject, beforePhase) || beforePhase != 2) Observe(task, false);
    }
    const auto result = originalBattle(command, mode, task);
    // State 1 -> 2 is an unconditional scheduling step. Only a call which
    // enters AND leaves state 2 proves the original resource predicate waited.
    if (mode == 2) Observe(task, false, beforePhase == 2 ? beforeObject : 0);
    return result;
}
bool ReadDungeon(void* core, bool& eligible, uint32_t& player) noexcept {
    eligible = false; player = 0;
    __try {
        auto* p = static_cast<unsigned char*>(core);
        if (!p || *reinterpret_cast<uint32_t*>(p) != 4 || p[0x790] != 0 ||
            *reinterpret_cast<uint32_t*>(p+0x4c) || *reinterpret_cast<uint32_t*>(p+0x54) ||
            *reinterpret_cast<uint32_t*>(p+0x5c)) return true;
        const auto handle = *reinterpret_cast<uint32_t*>(p+0xc);
        if (!handle) return true;
        player = *reinterpret_cast<uint32_t*>(handle+0x24);
        if (!player) return true;
        eligible = *reinterpret_cast<uint32_t*>(player) == reinterpret_cast<uintptr_t>(p+0xc) &&
            *reinterpret_cast<uint32_t*>(player+4) <= 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ObserveDungeon(void* core, bool waiting, bool readable = true) noexcept {
    const auto key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(core));
    AcquireSRWLockExclusive(&stateLock);
    const auto before = timing.Sample().transitions;
    if (!readable) timing.Fail(LoadFault::Memory, timing.Sample().lastQpc);
    else timing.Observe(key, key, waiting, Now(), LoadCoverageDungeonMap);
    Publish();
    const auto sample = timing.Sample();
    ReleaseSRWLockExclusive(&stateLock);
    if (trace && before != sample.transitions)
        Log("LoadTiming transition=%u reason=%u completed=%llu open=%llu qpc=%llu dungeon=%08x",
            sample.transitions, sample.reason, sample.completed, sample.opened, sample.lastQpc, key);
}
uint32_t __cdecl SetupHook() {
    auto* scope = enabled.load(std::memory_order_acquire) ? dungeonScope : nullptr;
    bool before = false, after = false; uint32_t playerBefore = 0, playerAfter = 0;
    const bool readableBefore = !scope || ReadDungeon(scope->core, before, playerBefore);
    const auto result = originalSetup(); // Exactly one original call; preserve all return bits.
    if (scope) {
        const bool readableAfter = ReadDungeon(scope->core, after, playerAfter);
        ++scope->calls;
        scope->player = playerAfter;
        scope->waiting = readableBefore && readableAfter && before && after &&
            playerBefore == playerAfter && (result & 0xff) == 1;
        if (!scope->waiting || scope->calls != 1)
            ObserveDungeon(scope->core, false, readableBefore && readableAfter);
    }
    return result;
}
void __cdecl DungeonHook(void* core) {
    if (!enabled.load(std::memory_order_acquire)) { originalDungeon(core); return; }
    bool eligible = false; uint32_t player = 0;
    const bool readable = ReadDungeon(core, eligible, player);
    if (!readable || !eligible) ObserveDungeon(core, false, readable);
    DungeonScope scope{core};
    auto* previous = dungeonScope;
    if (previous) previous->ambiguous = true;
    dungeonScope = &scope;
    bool returned = false;
    __try { originalDungeon(core); returned = true; }
    __finally {
        dungeonScope = previous;
        bool after = false; uint32_t playerAfter = 0;
        const bool readableAfter = !returned || ReadDungeon(core, after, playerAfter);
        ObserveDungeon(core, returned && scope.calls == 1 && scope.waiting && !scope.ambiguous &&
            after && playerAfter == scope.player, readableAfter);
    }
}
void __cdecl DestroyDungeonHook(void* core) {
    if (enabled.load(std::memory_order_acquire)) ObserveDungeon(core, false);
    originalDestroy(core);
}
bool Foreground() noexcept {
#ifdef VII_LOAD_TIMING_TESTING
    return true;
#else
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
#endif
}
void ObserveWorld(const WorldIdentity& owner, bool waiting, bool readable = true) noexcept {
    AcquireSRWLockExclusive(&stateLock);
    const auto before = timing.Sample().transitions;
    if (!readable) timing.Fail(LoadFault::Memory, timing.Sample().lastQpc);
    else timing.Observe(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner.task)),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner.object)), waiting, Now(), LoadCoverageWorldResources);
    Publish();
    const auto sample = timing.Sample();
    ReleaseSRWLockExclusive(&stateLock);
    if (trace && before != sample.transitions)
        Log("LoadTiming transition=%u reason=%u completed=%llu open=%llu qpc=%llu world=%08x",
            sample.transitions, sample.reason, sample.completed, sample.opened, sample.lastQpc,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(owner.task)));
}
bool ReadWorld(const WorldIdentity& owner, bool& eligible) noexcept {
    eligible = false;
    __try {
        auto* p = static_cast<unsigned char*>(owner.object);
        auto* root = worldRoot ? static_cast<unsigned char*>(*worldRoot) : nullptr;
        if (!p || !owner.task || !root) return true;
        eligible = *reinterpret_cast<void**>(static_cast<unsigned char*>(owner.task)+0x24) == p &&
            *reinterpret_cast<void**>(p+0x164) == owner.task &&
            *reinterpret_cast<void**>(root+0x16204) == owner.task &&
            *reinterpret_cast<void**>(p+0x20c) == worldMainCallback && *reinterpret_cast<uint32_t*>(p) == 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* __fastcall WorldDataHook(void* manager, void*, void* task) {
    const bool active = enabled.load(std::memory_order_acquire);
    if (active) ObserveWorld({task, nullptr}, false);
    worldIdentity = {};
    void* result = originalWorldData(manager, task);
    if (active) worldIdentity = {task, result};
    return result;
}
uint32_t __cdecl WorldMapHook(void* map) {
    const auto result = originalWorldMap(map);
    if (worldProof) { ++worldProof->mapCalls; worldProof->mapBusy = (result & 0xff) == 1; }
    return result;
}
uint32_t __cdecl CharacterBusyHook(void* object, uint32_t copy) {
    const auto result = originalCharacterBusy(object, copy);
    if (characterProof && (result & 0xff) == 1) characterProof->busy = true;
    return result;
}
uint32_t __cdecl WorldCharacterHook(void* object) {
    if (!worldProof) return originalWorldCharacter(object);
    CharacterProof proof;
    auto* previous = characterProof;
    if (previous) worldProof->ambiguous = true;
    characterProof = &proof;
    uint32_t result = 0;
    __try { result = originalWorldCharacter(object); }
    __finally { characterProof = previous; }
    ++worldProof->characterCalls;
    if ((result & 0xff) == 0 && proof.busy) worldProof->characterBusy = true;
    return result;
}
uint32_t __cdecl WorldReadyHook() {
    const auto owner = worldIdentity;
    worldIdentity = {}; // Never reuse evidence across callbacks.
    if (!enabled.load(std::memory_order_acquire)) return originalWorldReady();
    bool before = false;
    const bool readableBefore = ReadWorld(owner, before);
    if (!readableBefore || !before) {
        ObserveWorld(owner, false, readableBefore);
        return originalWorldReady();
    }
    WorldProof proof;
    auto* previous = worldProof;
    if (previous) previous->ambiguous = true;
    worldProof = &proof;
    uint32_t result = 0; bool returned = false;
    __try { result = originalWorldReady(); returned = true; }
    __finally {
        worldProof = previous;
        bool after = false;
        const bool readable = !returned || ReadWorld(owner, after);
        const bool resource = proof.mapCalls == 1 &&
            ((proof.mapBusy && proof.characterCalls == 0) ||
             (!proof.mapBusy && proof.characterCalls >= 1 && proof.characterCalls <= 2 && proof.characterBusy));
        ObserveWorld(owner, returned && (result & 0xff) == 0 && resource && after && !proof.ambiguous, readable);
    }
    return result;
}
// This runs at the original main-thread setup call. Read only the authoritative
// live root and its dependency chain; never retain or follow heap owners later.
bool ReadTitleSetup(void* video, uint32_t& task, uint32_t& object, bool& eligible) noexcept {
    task = object = 0; eligible = false;
    __try {
        auto* t = titleRoot ? static_cast<unsigned char*>(*titleRoot) : nullptr;
        if (!t || !video) return true;
        auto* p = *reinterpret_cast<unsigned char**>(t+0x24);
        if (!p || *reinterpret_cast<void**>(p+0x30) != t ||
            *reinterpret_cast<void**>(p+0x48) != titleMainCallback ||
            *reinterpret_cast<uint32_t*>(p) != 1) return true;
        auto* window = *reinterpret_cast<unsigned char**>(p+0x44);
        if (!window) return true;
        auto* data = *reinterpret_cast<unsigned char**>(window+0x6c);
        if (!data || *reinterpret_cast<uint32_t*>(data+0x10) != 2) return true;
        auto* movie = *reinterpret_cast<void***>(data+0xc);
        if (!movie || *movie != video) return true;
        auto* v = static_cast<unsigned char*>(video);
        auto* decoder = *reinterpret_cast<unsigned char**>(v+4);
        if (!decoder || *reinterpret_cast<uint32_t*>(v+0x78) != 4 || v[0x54] != 1 ||
            *reinterpret_cast<uint32_t*>(decoder+4) != 2 ||
            *reinterpret_cast<uint32_t*>(v+0x58) || *reinterpret_cast<uint32_t*>(v+0x4d8) || v[0x4dc]) return true;
        task = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t));
        object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); eligible = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ObserveTitle(uint32_t task, uint32_t object, bool waiting, bool readable = true) noexcept {
    AcquireSRWLockExclusive(&stateLock);
    const auto before = timing.Sample().transitions;
    if (!readable) timing.Fail(LoadFault::Memory, timing.Sample().lastQpc);
    else timing.Observe(task, object, waiting, Now(), LoadCoverageTitleMovieSetup);
    Publish(); const auto sample = timing.Sample();
    ReleaseSRWLockExclusive(&stateLock);
    if (trace && before != sample.transitions)
        Log("LoadTiming transition=%u reason=%u completed=%llu open=%llu qpc=%llu title=%08x",
            sample.transitions, sample.reason, sample.completed, sample.opened, sample.lastQpc, task);
}
uint32_t __cdecl TitleSetupHook(void* video) {
    if (!enabled.load(std::memory_order_acquire) || inTitleSetup) return originalTitleSetup(video);
    uint32_t task = 0, object = 0; bool eligible = false;
    const bool readable = ReadTitleSetup(video, task, object, eligible);
    if (!readable) ObserveTitle(0,0,false,false);
    if (!eligible) return originalTitleSetup(video);
    uint32_t result = 0;
    inTitleSetup = true;
    ObserveTitle(task,object,true);
    __try { result = originalTitleSetup(video); }
    __finally { ObserveTitle(task,object,false); inTitleSetup = false; }
    return result;
}
// Only the original load-list creation scan can enable these five callsites.
// Bracket each synchronous metadata call, leaving formatting/logging counted.
bool ReadSaveScope(bool& eligible) noexcept {
    eligible = false;
    __try {
        if (saveMode && saveState && savePhase && saveList)
            eligible = *saveMode == 0 && *saveState == 1 && *savePhase == 0 && !*saveList;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ObserveSave(SaveScope* scope, bool waiting, bool readable = true) noexcept {
    const auto key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scope));
    AcquireSRWLockExclusive(&stateLock);
    if (!readable) timing.Fail(LoadFault::Memory,timing.Sample().lastQpc);
    else timing.Observe(key,key,waiting,Now(),LoadCoverageSaveMetadata);
    Publish();
    ReleaseSRWLockExclusive(&stateLock);
    // No per-call logging: a scan can execute hundreds of short operations.
}
SaveScope* BeginSaveIo() noexcept {
    auto* scope = enabled.load(std::memory_order_acquire) ? saveScope : nullptr;
    if (!scope || !scope->eligible || scope->ioActive) return nullptr;
    bool eligible = false;
    const bool readable = ReadSaveScope(eligible);
    if (!readable) ObserveSave(scope,false,false);
    if (!eligible) return nullptr;
    scope->ioActive = true; ObserveSave(scope,true); return scope;
}
void EndSaveIo(SaveScope* scope) noexcept {
    if (scope && scope->ioActive) { ObserveSave(scope,false); scope->ioActive = false; }
}
void __cdecl SaveScanHook() {
    if (!enabled.load(std::memory_order_acquire)) { originalSaveScan(); return; }
    SaveScope scope;
    auto* previous = saveScope;
    const bool readable = ReadSaveScope(scope.eligible);
    if (!readable) ObserveSave(&scope,false,false);
    // Nested scans cannot borrow an outer scan's qualification.
    if (previous) scope.eligible = false;
    saveScope = &scope;
    __try { originalSaveScan(); }
    __finally { EndSaveIo(&scope); saveScope = previous; }
}
uint32_t __cdecl SaveExistsHook(uint32_t path) {
    auto* scope = BeginSaveIo(); uint32_t result = 0;
    __try { result = originalSaveExists(path); }
    __finally { EndSaveIo(scope); }
    return result;
}
uint32_t __cdecl SaveOpenHook(uint32_t path,uint32_t flags,uint32_t mode) {
    auto* scope = BeginSaveIo(); uint32_t result = 0;
    __try { result = originalSaveOpen(path,flags,mode); }
    __finally { EndSaveIo(scope); }
    return result;
}
uint32_t __cdecl SaveReadHook(uint32_t handle,uint32_t buffer,uint32_t bytes) {
    auto* scope = BeginSaveIo(); uint32_t result = 0;
    __try { result = originalSaveRead(handle,buffer,bytes); }
    __finally { EndSaveIo(scope); }
    return result;
}
uint32_t __cdecl SaveCloseHook(uint32_t handle) {
    auto* scope = BeginSaveIo(); uint32_t result = 0;
    __try { result = originalSaveClose(handle); }
    __finally { EndSaveIo(scope); }
    return result;
}
void ObserveAdv(void* task, void* object, bool waiting, bool readable = true) noexcept {
    AcquireSRWLockExclusive(&stateLock);
    const auto before = timing.Sample().transitions;
    if (!readable) timing.Fail(LoadFault::Memory, timing.Sample().lastQpc);
    else timing.Observe(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(task)),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)), waiting, Now(), LoadCoverageAdvScript);
    Publish();
    const auto sample = timing.Sample();
    ReleaseSRWLockExclusive(&stateLock);
    if (trace && before != sample.transitions)
        Log("LoadTiming transition=%u reason=%u completed=%llu open=%llu qpc=%llu adv=%08x",
            sample.transitions, sample.reason, sample.completed, sample.opened, sample.lastQpc,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(task)));
}
bool ReadAdv(const AdvScope& scope, bool& eligible, uint32_t& scene, uint32_t& phase) noexcept {
    eligible = false; scene = phase = 0;
    __try {
        auto* p = static_cast<unsigned char*>(scope.object);
        if (!scope.task || !p || *reinterpret_cast<void**>(static_cast<unsigned char*>(scope.task)+0x24) != p ||
            *reinterpret_cast<void**>(p) != scope.task) return true;
        phase = p[0x375c];
        if (phase != 3 && phase != 4) return true;
        scene = *reinterpret_cast<uint32_t*>(p+0x10); eligible = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Three call sites inside the original ADV callback avoid competing with the
// EventSkipBuffer entry detour. The first call runs for every command, including
// creation/destruction; close before the original can release or replace state.
void* __fastcall AdvDataHook(void* manager, void*, void* task) {
    const bool active = enabled.load(std::memory_order_acquire);
    if (active) ObserveAdv(task, nullptr, false);
    advScope = {};
    void* result = originalAdvData(manager, task);
    if (active) {
        advScope.task = task; advScope.object = result;
        bool eligible = false;
        if (!ReadAdv(advScope, eligible, advScope.scene, advScope.phase)) ObserveAdv(task, nullptr, false, false);
        if (!eligible) advScope = {};
    }
    return result;
}
uint32_t __fastcall AdvScriptHook(void* controller) {
    const auto result = originalScriptBusy(controller);
    if (enabled.load(std::memory_order_acquire) && advScope.task) {
        ++advScope.calls;
        advScope.waiting = advScope.phase == 3 && (result & 0xff) == 1;
    }
    return result;
}
uint32_t __cdecl AdvQueueHook(uint32_t category) {
    const auto result = originalAdvQueue(category);
    if (enabled.load(std::memory_order_acquire) && advScope.task) {
        ++advScope.calls;
        advScope.waiting = advScope.phase == 4 && category == 3 && (result & 0xff) == 1;
    }
    return result;
}
uint32_t __cdecl AdvCancelHook() {
    const auto result = originalAdvCancel();
    const auto scope = advScope; advScope = {};
    if (enabled.load(std::memory_order_acquire) && scope.task) {
        bool eligible = false; uint32_t scene = 0, phase = 0;
        const bool readable = (result & 0xff) != 0 || ReadAdv(scope, eligible, scene, phase);
        ObserveAdv(scope.task, scope.object, (result & 0xff) == 0 && readable && eligible &&
            scene == scope.scene && phase == scope.phase && scope.calls == 1 && scope.waiting, readable);
    }
    return result;
}
void __fastcall ClockHook(void* context) {
    if (!enabled.load(std::memory_order_acquire)) { originalClock(context); return; }
    AcquireSRWLockExclusive(&stateLock);
    timing.Frame(Now(), Foreground());
    const auto sample = timing.Sample();
    auto* fields = static_cast<uint32_t*>(context);
    const auto interval = fields[8];
    const bool change = CanUncapLoad(sample, requestedFps, globalFps, fields[7], interval);
    if (change) ++payload.fpsApplications;
    Publish();
    ReleaseSRWLockExclusive(&stateLock);
    // The native zero-interval branch computes real elapsed time. Restore on
    // every exit; never change the interpolation numerator or audio patch.
    if (change) fields[8] = 0;
    __try { originalClock(context); }
    __finally { if (change) fields[8] = interval; }
}
bool Good(const CallReport& r) noexcept {
    return r.installed && r.status == CallStatus::Installed && r.protectionRestored && !r.resumeFailures;
}
bool MakeTrampoline(unsigned char* entry) noexcept {
    // Verified whole instructions: push ebp; mov ebp,esp; push [ebp+10h].
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) return false;
    std::memcpy(code, entry, 6); code[6] = 0xe9;
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry + 6) - reinterpret_cast<uintptr_t>(code + 11));
    std::memcpy(code + 7, &relative, sizeof(relative));
    DWORD prior = 0;
    if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &prior) || !FlushInstructionCache(GetCurrentProcess(), code, 11)) {
        VirtualFree(code, 0, MEM_RELEASE); return false;
    }
    originalBattle = reinterpret_cast<BattleCallback>(code);
    return true;
}
}

void NotifyLoadTimingNewGame(uint32_t sequence) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&stateLock);
    const auto q = Now();
    payload.newGameSequence = sequence; payload.newGameQpc = q;
    payload.newGameExcludedTicks = timing.Total(q);
    Publish();
    ReleaseSRWLockExclusive(&stateLock);
}

bool InstallLoadTiming(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    requestedFps = Option(context, L"Patches", L"UnlockFPSDuringLoads", 0) != 0;
    globalFps = Option(context, L"Patches", L"Neptasm", 0) != 0 && Option(context, L"Neptasm", L"FPSUnlock", 0) != 0;
    trace = Option(context, L"LoadTiming", L"Trace", 0) != 0;
    auto* callback = reinterpret_cast<unsigned char*>(base + 0x258e40);
    auto* clockCall = reinterpret_cast<unsigned char*>(base + 0x3af891);
    const std::array<unsigned char,8> callbackBytes{0x55,0x8b,0xec,0xff,0x75,0x10,0x8b,0x0d};
    const std::array<unsigned char,8> clockBytes{0xe8,0x2a,0x52,0x05,0x00,0xe8,0x65,0xac};
    auto* dungeonCall = reinterpret_cast<unsigned char*>(base+0x2a3841);
    auto* destroyCall = reinterpret_cast<unsigned char*>(base+0x2a3863);
    auto* setupCall = reinterpret_cast<unsigned char*>(base+0x2a31a6);
    const std::array<unsigned char,8> dungeonBytes{0xe8,0x7a,0xfe,0xff,0xff,0x83,0xc4,0x04};
    const std::array<unsigned char,8> destroyBytes{0xe8,0xf8,0xfc,0xff,0xff,0x8b,0x0d,0x88};
    const std::array<unsigned char,8> setupBytes{0xe8,0xb5,0x31,0xfa,0xff,0x3c,0x01,0x75};
    const CallSite advSites[]{
        {reinterpret_cast<unsigned char*>(base+0x8e5af), {0xe8,0xcc,0x94,0xff,0xff,0x8b,0xf0,0x8b}, reinterpret_cast<void*>(AdvDataHook)},
        {reinterpret_cast<unsigned char*>(base+0x8e698), {0xe8,0x93,0x14,0x00,0x00,0x84,0xc0,0x0f}, reinterpret_cast<void*>(AdvScriptHook)},
        {reinterpret_cast<unsigned char*>(base+0x8e85f), {0xe8,0x5c,0x92,0x0a,0x00,0x84,0xc0,0x75}, reinterpret_cast<void*>(AdvCancelHook)}};
    const CallSite advQueueSite{reinterpret_cast<unsigned char*>(base+0x8e6ee),
        {0xe8,0x9d,0xe1,0x30,0x00,0x83,0xc4,0x04},reinterpret_cast<void*>(AdvQueueHook)};
    if (std::memcmp(advQueueSite.address,advQueueSite.expected.data(),8)) {
        timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming ADV queue signature unavailable; disabled"); return false;
    }
    const CallSite worldSites[]{
        {reinterpret_cast<unsigned char*>(base+0x2bc385), {0xe8,0xf6,0xb6,0xdc,0xff,0x8b,0xf0,0x8b}, reinterpret_cast<void*>(WorldDataHook)},
        {reinterpret_cast<unsigned char*>(base+0x2b32cd), {0xe8,0x0e,0x41,0xf9,0xff,0x84,0xc0,0x0f}, reinterpret_cast<void*>(WorldReadyHook)},
        {reinterpret_cast<unsigned char*>(base+0x2a9f0d), {0xe8,0x9e,0xfd,0xff,0xff,0x83,0xc4,0x04}, reinterpret_cast<void*>(WorldMapHook)},
        {reinterpret_cast<unsigned char*>(base+0x24b4c3), {0xe8,0xd8,0x34,0xea,0xff,0x83,0xc4,0x04}, reinterpret_cast<void*>(WorldCharacterHook)},
        {reinterpret_cast<unsigned char*>(base+0xee9c6), {0xe8,0x55,0x6d,0x00,0x00,0x83,0xc4,0x08}, reinterpret_cast<void*>(CharacterBusyHook)},
        {reinterpret_cast<unsigned char*>(base+0xee9f6), {0xe8,0x25,0x6d,0x00,0x00,0x83,0xc4,0x08}, reinterpret_cast<void*>(CharacterBusyHook)},
        {reinterpret_cast<unsigned char*>(base+0xeed2c), {0xe8,0xef,0x69,0x00,0x00,0x83,0xc4,0x08}, reinterpret_cast<void*>(CharacterBusyHook)}};
    const CallSite titleSite{reinterpret_cast<unsigned char*>(base+0x3cae8e),
        {0xe8,0x0d,0xf5,0xff,0xff,0x83,0xc4,0x04},reinterpret_cast<void*>(TitleSetupHook)};
    if (std::memcmp(titleSite.address,titleSite.expected.data(),8)) {
        timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming title signature unavailable; disabled"); return false;
    }
    const CallSite saveSites[]{
        {reinterpret_cast<unsigned char*>(base+0x47abd0), {0xe8,0xab,0xf9,0xff,0xff,0x8d,0x45,0xd4}, reinterpret_cast<void*>(SaveScanHook)},
        {reinterpret_cast<unsigned char*>(base+0x47a60c), {0xe8,0x5f,0x5d,0xef,0xff,0x83,0xc4,0x28}, reinterpret_cast<void*>(SaveExistsHook)},
        {reinterpret_cast<unsigned char*>(base+0x47a623), {0xe8,0x48,0x5d,0xef,0xff,0x83,0xc4,0x04}, reinterpret_cast<void*>(SaveExistsHook)},
        {reinterpret_cast<unsigned char*>(base+0x47a65b), {0xe8,0xe0,0x62,0xef,0xff,0x8b,0xf8,0x83}, reinterpret_cast<void*>(SaveOpenHook)},
        {reinterpret_cast<unsigned char*>(base+0x47a671), {0xe8,0x1a,0x5f,0xef,0xff,0x57,0xe8,0x74}, reinterpret_cast<void*>(SaveReadHook)},
        {reinterpret_cast<unsigned char*>(base+0x47a677), {0xe8,0x74,0x64,0xef,0xff,0x83,0xc4,0x10}, reinterpret_cast<void*>(SaveCloseHook)}};
    for (const auto& site : saveSites) if (std::memcmp(site.address,site.expected.data(),8)) {
        timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming save signatures unavailable; disabled"); return false;
    }
    const unsigned char clockEntry[]{0x55,0x8b,0xec,0x83,0xec,0x08,0x53,0x56};
    const unsigned char gate[]{0x8b,0x7e,0x20,0x85,0xff,0x0f,0x84,0x8a};
    LARGE_INTEGER qpf{};
    for (const auto& site : advSites) if (std::memcmp(site.address,site.expected.data(),8)) {
        timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming ADV signatures unavailable; disabled"); return false;
    }
    for (const auto& site : worldSites) if (std::memcmp(site.address,site.expected.data(),8)) {
        timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming world signatures unavailable; disabled"); return false;
    }
    if (std::memcmp(callback,callbackBytes.data(),8) || std::memcmp(clockCall,clockBytes.data(),8) ||
        std::memcmp(dungeonCall,dungeonBytes.data(),8) || std::memcmp(destroyCall,destroyBytes.data(),8) ||
        std::memcmp(setupCall,setupBytes.data(),8) ||
        std::memcmp(reinterpret_cast<void*>(base+0x404ac0),clockEntry,sizeof(clockEntry)) ||
        (requestedFps && !globalFps && std::memcmp(reinterpret_cast<void*>(base+0x404b4c),gate,sizeof(gate))) ||
        !QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0 || !MakeTrampoline(callback)) {
        timing.Fail(LoadFault::Install, 0); Publish();
        Log("LoadTiming signatures/clock/trampoline unavailable; disabled"); return false;
    }
    frequency = static_cast<uint64_t>(qpf.QuadPart);
    originalClock = reinterpret_cast<FrameClock>(base + 0x404ac0);
    originalDungeon = reinterpret_cast<DungeonCallback>(base+0x2a36c0);
    originalDestroy = reinterpret_cast<DungeonCallback>(base+0x2a3560);
    originalSetup = reinterpret_cast<SetupPredicate>(base+0x246360);
    originalAdvData = reinterpret_cast<TaskData>(base+0x87a80);
    originalScriptBusy = reinterpret_cast<ScriptBusy>(base+0x8fb30);
    originalAdvCancel = reinterpret_cast<SetupPredicate>(base+0x137ac0);
    originalAdvQueue = reinterpret_cast<QueueBusy>(base+0x39c890);
    originalWorldData = reinterpret_cast<TaskData>(base+0x87a80);
    originalWorldReady = reinterpret_cast<SetupPredicate>(base+0x2473e0);
    originalWorldMap = reinterpret_cast<ResourcePredicate>(base+0x2a9cb0);
    originalWorldCharacter = reinterpret_cast<ResourcePredicate>(base+0xee9a0);
    originalCharacterBusy = reinterpret_cast<CharacterBusy>(base+0xf5720);
    worldMainCallback = reinterpret_cast<void*>(base+0x2b3240);
    worldRoot = reinterpret_cast<void**>(base+0x706a78);
    originalTitleSetup = reinterpret_cast<ResourcePredicate>(base+0x3ca3a0);
    titleRoot = reinterpret_cast<void**>(base+0x705e68);
    titleMainCallback = reinterpret_cast<void*>(base+0x11b6a0);
    originalSaveScan = reinterpret_cast<SaveScan>(base+0x47a580);
    originalSaveExists = reinterpret_cast<SaveOne>(base+0x370370);
    originalSaveOpen = reinterpret_cast<SaveThree>(base+0x370940);
    originalSaveRead = reinterpret_cast<SaveThree>(base+0x370590);
    originalSaveClose = reinterpret_cast<SaveOne>(base+0x370af0);
    saveMode = reinterpret_cast<uint32_t*>(base+0x4818094);
    saveState = reinterpret_cast<uint32_t*>(base+0x4818070);
    savePhase = reinterpret_cast<uint32_t*>(base+0x4818080);
    saveList = reinterpret_cast<void**>(base+0x481808c);
    // All adapters pass through until the complete installation succeeds.
    const auto first = ReplaceEntryJump({callback,callbackBytes,reinterpret_cast<void*>(BattleHook)});
    if (!Good(first)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming callback=%s; disabled",CallStatusName(first.status)); return false; }
    const auto second = ReplaceCall({clockCall,clockBytes,reinterpret_cast<void*>(ClockHook)});
    if (!Good(second)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming clock=%s; disabled",CallStatusName(second.status)); return false; }
    const CallSite dungeonSites[]{
        {dungeonCall,dungeonBytes,reinterpret_cast<void*>(DungeonHook)},
        {destroyCall,destroyBytes,reinterpret_cast<void*>(DestroyDungeonHook)},
        {setupCall,setupBytes,reinterpret_cast<void*>(SetupHook)}};
    for (const auto& site : dungeonSites) {
        const auto report = ReplaceCall(site);
        if (!Good(report)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming dungeon=%s; disabled",CallStatusName(report.status)); return false; }
    }
    for (const auto& site : advSites) {
        const auto report = ReplaceCall(site);
        if (!Good(report)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming ADV=%s; disabled",CallStatusName(report.status)); return false; }
    }
    {
        const auto report = ReplaceCall(advQueueSite);
        if (!Good(report)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming ADV queue=%s; disabled",CallStatusName(report.status)); return false; }
    }
    for (const auto& site : worldSites) {
        const auto report = ReplaceCall(site);
        if (!Good(report)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming world=%s; disabled",CallStatusName(report.status)); return false; }
    }
    for (const auto& site : saveSites) {
        const auto report = ReplaceCall(site);
        if (!Good(report)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming save=%s; disabled",CallStatusName(report.status)); return false; }
    }
    const auto titleReport = ReplaceCall(titleSite);
    if (!Good(titleReport)) { timing.Fail(LoadFault::Install,0); Publish(); Log("LoadTiming title=%s; disabled",CallStatusName(titleReport.status)); return false; }
    AcquireSRWLockExclusive(&stateLock);
    // A suspended or unobserved >500ms frame during this asynchronous wait
    // invalidates timing rather than inventing load duration across the gap.
    timing.Enable(Now(), frequency / 2); Publish();
    ReleaseSRWLockExclusive(&stateLock);
    enabled.store(true,std::memory_order_release);
    Log("LoadTiming ready partial_coverage=%u fps_requested=%d global_fps=%d descriptor=%08x",
        LoadCoverageKnown, requestedFps, globalFps, static_cast<unsigned>(reinterpret_cast<uintptr_t>(&descriptor)));
    return true;
}
#ifdef VII_LOAD_TIMING_TESTING
void ResetLoadTimingTest(BattleCallback callback, FrameClock clock, bool fps, bool global) {
    AcquireSRWLockExclusive(&stateLock);
    originalBattle = callback; originalClock = clock; requestedFps = fps; globalFps = global;
    LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); frequency = static_cast<uint64_t>(f.QuadPart);
    payload = {}; timing.Enable(Now(),frequency/2); Publish();
    advScope = {}; inTitleSetup = false; saveScope = nullptr;
    worldIdentity = {}; worldProof = nullptr; characterProof = nullptr;
    enabled.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&stateLock);
}
uint32_t LoadBattleTest(uint32_t command, uint32_t mode, void* task) { return BattleHook(command,mode,task); }
void LoadClockTest(void* context) { ClockHook(context); }
void SetDungeonTimingTest(DungeonCallback update, DungeonCallback destroy, SetupPredicate setup) {
    originalDungeon = update; originalDestroy = destroy; originalSetup = setup;
}
void LoadDungeonTest(void* core) { DungeonHook(core); }
void DestroyDungeonTest(void* core) { DestroyDungeonHook(core); }
uint32_t LoadSetupTest() { return SetupHook(); }
void SetAdvTimingTest(TaskData data, ScriptBusy busy, SetupPredicate cancel) {
    originalAdvData = data; originalScriptBusy = busy; originalAdvCancel = cancel;
}
void SetAdvQueueTimingTest(QueueBusy queue) { originalAdvQueue = queue; }
uint32_t LoadAdvQueueTest(uint32_t category) { return AdvQueueHook(category); }
void* LoadAdvDataTest(void* manager, void* task) { return AdvDataHook(manager,nullptr,task); }
uint32_t LoadAdvScriptTest(void* controller) { return AdvScriptHook(controller); }
uint32_t LoadAdvCancelTest() { return AdvCancelHook(); }
void SetWorldTimingTest(TaskData data, SetupPredicate ready, ResourcePredicate map, ResourcePredicate character,
                        CharacterBusy busy, void** root, void* callback) {
    originalWorldData = data; originalWorldReady = ready; originalWorldMap = map;
    originalWorldCharacter = character; originalCharacterBusy = busy;
    worldRoot = root; worldMainCallback = callback;
}
void* LoadWorldDataTest(void* manager, void* task) { return WorldDataHook(manager,nullptr,task); }
uint32_t LoadWorldReadyTest() { return WorldReadyHook(); }
uint32_t LoadWorldMapTest(void* object) { return WorldMapHook(object); }
uint32_t LoadWorldCharacterTest(void* object) { return WorldCharacterHook(object); }
uint32_t LoadWorldBusyTest(void* object, uint32_t copy) { return CharacterBusyHook(object,copy); }
void SetTitleTimingTest(ResourcePredicate setup, void** root, void* callback) {
    originalTitleSetup = setup; titleRoot = root; titleMainCallback = callback;
}
uint32_t LoadTitleSetupTest(void* video) { return TitleSetupHook(video); }
void SetSaveTimingTest(SaveScan scan, SaveOne exists, SaveThree open, SaveThree read, SaveOne close,
                       uint32_t* mode, uint32_t* state, uint32_t* phase, void** list) {
    originalSaveScan=scan; originalSaveExists=exists; originalSaveOpen=open;
    originalSaveRead=read; originalSaveClose=close;
    saveMode=mode; saveState=state; savePhase=phase; saveList=list;
}
void LoadSaveScanTest() { SaveScanHook(); }
uint32_t LoadSaveExistsTest(uint32_t path) { return SaveExistsHook(path); }
uint32_t LoadSaveOpenTest(uint32_t path,uint32_t flags,uint32_t mode) { return SaveOpenHook(path,flags,mode); }
uint32_t LoadSaveReadTest(uint32_t handle,uint32_t buffer,uint32_t bytes) { return SaveReadHook(handle,buffer,bytes); }
uint32_t LoadSaveCloseTest(uint32_t handle) { return SaveCloseHook(handle); }
LoadTimingPayload ReadLoadTimingTest() {
    AcquireSRWLockShared(&stateLock); const auto result = bridge.data; ReleaseSRWLockShared(&stateLock);
    return result;
}
#endif
}
