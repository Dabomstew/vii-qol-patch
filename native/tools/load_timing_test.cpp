#include "load_timing.hpp"
#include <cstdio>
#include <cstdlib>

namespace vii {
void Log(const char*, ...) noexcept {}
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); std::exit(1); } } while(false)

static void CoreTests() {
    LoadTimingCore core;
    CHECK(core.Sample().status == LoadStatus::Disabled);
    core.Enable(100,1000); core.Frame(110,true);
    core.Observe(1,2,false,115); CHECK(core.Total(120)==0); // Playable/background activity.
    core.Observe(1,2,true,120); CHECK(core.Total(130)==10);
    const auto anchor=core.Total(125); // Start inside the open load.
    core.Observe(3,4,true,130); CHECK(core.Sample().owners==2);
    core.Cancel(1,140); CHECK(core.Sample().reason==1 && core.Total(150)==30);
    core.Cancel(3,160); CHECK(core.Total(170)==40 && core.Total(170)-anchor==35);
    CHECK(core.Sample().transitions==2); // Union, not nested duration sum.
    core.Observe(5,6,true,180); core.Observe(5,6,false,181);
    CHECK(core.Total(250)==41); // Completed sub-poll interval survives.
    core.Observe(5,6,true,260); const auto gen=core.Sample().generation;
    core.Observe(5,7,true,270); CHECK(core.Sample().generation==gen+1);
    core.Cancel(5,280); CHECK(core.Total(300)==61);
    core.Frame(310,true); core.Observe(5,7,true,315); core.Frame(320,false);
    CHECK(core.Sample().reason==0 && core.Total(350)==66);
    core.Observe(5,7,true,355); CHECK(core.Sample().reason==0);
    core.Frame(400,true); CHECK(core.Sample().reason==0); // Require fresh evidence on regain.
    core.Observe(5,7,true,410); core.Frame(420,true); core.Frame(430,true);
    CHECK(core.Sample().status==LoadStatus::Fault && core.Sample().fault==LoadFault::MissingOwner);
    CHECK(core.Total(999)==66); // Unknown tail excluded, both consumers disabled.
    core.Enable(100,50); core.Frame(101,true); core.Observe(1,2,true,105); core.Observe(1,2,true,110);
    core.Frame(170,true); CHECK(core.Sample().fault==LoadFault::LongGap && core.Total(999)==5);
    core.Enable(100,1000); core.Observe(1,2,true,120); core.Observe(1,2,true,130); core.Frame(110,true);
    CHECK(core.Sample().fault==LoadFault::Clock && core.Total(999)==10);
    core.Enable(100,1000);
    for(uint32_t i=1;i<=8;++i)core.Observe(i,i,true,100+i);
    core.Observe(9,9,true,109); CHECK(core.Sample().fault==LoadFault::Capacity);
    auto sample=core.Sample(); CHECK(!CanUncapLoad(sample,true,false,0,16666));
    core.Enable(100,1000); core.Observe(1,2,true,110); sample=core.Sample();
    CHECK(CanUncapLoad(sample,true,false,0,16666));
    CHECK(!CanUncapLoad(sample,false,false,0,16666));
    CHECK(!CanUncapLoad(sample,true,true,0,16666));
    CHECK(!CanUncapLoad(sample,true,false,1,16666));
    CHECK(!CanUncapLoad(sample,true,false,0,0));
    core.Cancel(1,120); CHECK(!CanUncapLoad(core.Sample(),true,false,0,16666));
    core.Enable(100,1000);
    core.Observe(1,2,true,110,LoadCoverageDungeonMap);
    core.Observe(1,3,true,120); // Same numeric identity in independent adapters.
    CHECK(core.Sample().reason==3 && core.Sample().owners==2 && core.Total(130)==20);
    core.Cancel(1,140,LoadCoverageDungeonMap);
    CHECK(core.Sample().reason==1 && core.Total(150)==40);
    core.Cancel(1,160); CHECK(core.Total(999)==50 && core.Sample().transitions==4);
    core.Enable(100,1000);
    for(auto reason : {1u,2u,4u})core.Observe(1,reason,true,110,reason);
    CHECK(core.Sample().reason==7 && core.Sample().owners==3 && core.Total(120)==10);
    core.Cancel(1,120,LoadCoverageAdvScript); CHECK(core.Sample().reason==3);
    core.Observe(9,9,true,130,64); CHECK(core.Sample().owners==2); // Unknown reason rejected.
    core.Enable(100,1000);
    for(auto reason : {1u,2u,4u,8u})core.Observe(1,reason,true,110,reason);
    CHECK(core.Sample().reason==15 && core.Sample().owners==4 && core.Total(120)==10);
    core.Observe(1,16,true,115,LoadCoverageTitleMovieSetup);
    CHECK(core.Sample().reason==31 && core.Sample().owners==5 && core.Total(120)==10);
    core.Observe(1,32,true,115,LoadCoverageSaveMetadata);
    CHECK(core.Sample().reason==63 && core.Sample().owners==6 && core.Total(120)==10);
    core.Cancel(1,120,LoadCoverageSaveMetadata); CHECK(core.Sample().reason==31);
    core.Cancel(1,120,LoadCoverageTitleMovieSetup); CHECK(core.Sample().reason==15);
    core.Cancel(1,120,LoadCoverageWorldResources); CHECK(core.Sample().reason==7);
}

static uint32_t object[4]{}, task[10]{}, clockFields[12]{};
static uint32_t nextPhase=2, seenInterval=0, originalCalls=0;
static bool clockThrows=false;
static uint32_t __cdecl OriginalBattle(uint32_t command,uint32_t mode,void* handle) {
    CHECK(command==77 && handle==task); ++originalCalls;
    if(mode==2)object[1]=nextPhase;
    return 0x1234abcd;
}
static void __fastcall OriginalClock(void* context) {
    CHECK(context==clockFields); seenInterval=clockFields[8];
    clockFields[6]=123; // Preserve unrelated clock outputs.
    if(clockThrows)RaiseException(0xe0420001,0,0,nullptr);
}
static void ExceptionalClock() {
    __try { LoadClockTest(clockFields); CHECK(false); }
    __except(GetExceptionCode()==0xe0420001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void AdapterTests() {
    task[9]=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    clockFields[8]=16666; object[1]=1; nextPhase=2;
    CHECK(LoadBattleTest(77,2,task)==0x1234abcd && originalCalls==1);
    CHECK(ReadLoadTimingTest().reason==0); // Unconditional state 1 -> 2 is not proof of waiting.
    LoadBattleTest(77,2,task);
    CHECK(ReadLoadTimingTest().reason==1);
    LoadClockTest(clockFields); CHECK(seenInterval==0 && clockFields[8]==16666 && clockFields[6]==123);
    CHECK(ReadLoadTimingTest().fpsApplications==1);
    LoadBattleTest(77,2,task); clockThrows=true; ExceptionalClock(); clockThrows=false;
    CHECK(clockFields[8]==16666); // Restore on exceptional return too.
    LoadBattleTest(77,2,task); clockFields[7]=1; LoadClockTest(clockFields);
    CHECK(seenInterval==16666 && clockFields[8]==16666); clockFields[7]=0;
    nextPhase=3; LoadBattleTest(77,2,task); CHECK(ReadLoadTimingTest().reason==0);
    LoadClockTest(clockFields); CHECK(seenInterval==16666); // Following camera stays capped.
    NotifyLoadTimingNewGame(9); auto sample=ReadLoadTimingTest();
    CHECK(sample.newGameSequence==9 && sample.newGameQpc>0 && sample.newGameExcludedTicks==sample.completedTicks);
    nextPhase=2; LoadBattleTest(77,2,task); LoadBattleTest(77,1,task);
    CHECK(ReadLoadTimingTest().reason==0); // Destruction before another frame.
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,true); LoadBattleTest(77,2,task);
    LoadClockTest(clockFields); CHECK(seenInterval==16666 && ReadLoadTimingTest().fpsApplications==0);
    CHECK(ReadLoadTimingTest().reason==1 && ReadLoadTimingTest().fpsGlobal==1);
    ResetLoadTimingTest(OriginalBattle,OriginalClock,false,false); LoadBattleTest(77,2,task);
    LoadClockTest(clockFields); CHECK(seenInterval==16666 && ReadLoadTimingTest().reason==1);
    // A bad observer read disables timing while still forwarding the callback.
    task[9]=1; LoadBattleTest(77,0,task); nextPhase=3;
    LoadBattleTest(77,2,task); CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault));
    LoadClockTest(clockFields); CHECK(seenInterval==16666);
}
static uint32_t dungeon[0x800/4]{}, playerHandle[10]{}, playerData[4]{};
static uint32_t setupResult=0xabcdef01, setupCalls=0, dungeonCalls=0, destroyed=0;
static unsigned predicateCalls=1;
static bool dungeonThrows=false, replacePlayer=false;
static uint32_t dungeonAfter=4;
static uint32_t __cdecl OriginalSetup() { ++setupCalls; return setupResult; }
static void __cdecl OriginalDungeon(void* p) {
    CHECK(p==dungeon); ++dungeonCalls;
    for(unsigned i=0;i<predicateCalls;++i) CHECK(LoadSetupTest()==setupResult);
    if(dungeonThrows) RaiseException(0xe0420002,0,0,nullptr);
    dungeon[0]=dungeonAfter;
    if(replacePlayer) playerHandle[9]=0;
}
static void __cdecl OriginalDestroy(void* p) {
    CHECK(p==dungeon && ReadLoadTimingTest().reason==0); ++destroyed;
}
static void ExceptionalDungeon() {
    __try { LoadDungeonTest(dungeon); CHECK(false); }
    __except(GetExceptionCode()==0xe0420002?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void DungeonTests() {
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    SetDungeonTimingTest(OriginalDungeon,OriginalDestroy,OriginalSetup);
    dungeon[0]=4; dungeon[3]=reinterpret_cast<uintptr_t>(playerHandle);
    playerHandle[9]=reinterpret_cast<uintptr_t>(playerData);
    playerData[0]=reinterpret_cast<uintptr_t>(dungeon+3); playerData[1]=1;
    LoadDungeonTest(dungeon);
    CHECK(setupCalls==1 && dungeonCalls==1 && ReadLoadTimingTest().reason==2);
    CHECK(ReadLoadTimingTest().coverage==63);
    LoadClockTest(clockFields); CHECK(seenInterval==0 && clockFields[8]==16666);
    setupResult=0xabcdef00; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0);
    setupResult=0xabcdef01;
    for(auto phase : {3u,8u,9u}) {
        dungeon[0]=phase; dungeonAfter=phase; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0);
    }
    dungeon[0]=dungeonAfter=4;
    for(auto offset : {0x4cu/4,0x54u/4,0x5cu/4,0x790u/4}) {
        dungeon[offset]=1; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0); dungeon[offset]=0;
    }
    playerData[1]=2; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0); playerData[1]=0;
    predicateCalls=0; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0);
    predicateCalls=2; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0); predicateCalls=1;
    dungeonAfter=9; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0); dungeon[0]=dungeonAfter=4;
    replacePlayer=true; LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==0);
    replacePlayer=false; playerHandle[9]=reinterpret_cast<uintptr_t>(playerData);
    LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==2);
    dungeonThrows=true; ExceptionalDungeon(); dungeonThrows=false;
    CHECK(ReadLoadTimingTest().reason==0);
    CHECK(LoadSetupTest()==setupResult && ReadLoadTimingTest().reason==0); // No dangling stack scope.
    LoadDungeonTest(dungeon); DestroyDungeonTest(dungeon); CHECK(destroyed==1);
    LoadDungeonTest(dungeon); CHECK(ReadLoadTimingTest().reason==2); // Reused owner needs new proof.
    dungeon[3]=1; LoadDungeonTest(dungeon);
    CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault));
    LoadClockTest(clockFields); CHECK(seenInterval==16666);
}
static uint32_t advTask[10]{}, advObject[0x3760/4]{}, advOther[0x3760/4]{};
static uint32_t advBusy=0x12340001, advCancel=0xabcd0000, advDataCalls=0, advBusyCalls=0, advCancelCalls=0;
static void* __fastcall OriginalAdvData(void* manager, void*, void* handle) {
    CHECK(manager==task && handle==advTask); ++advDataCalls;
    CHECK(ReadLoadTimingTest().reason==0); // Closes even before destruction/replacement.
    return reinterpret_cast<void*>(advTask[9]);
}
static uint32_t __fastcall OriginalAdvBusy(void* controller) {
    CHECK(controller==object); ++advBusyCalls; return advBusy;
}
static uint32_t __cdecl OriginalAdvCancel() { ++advCancelCalls; return advCancel; }
static void AdvBegin() { CHECK(LoadAdvDataTest(task,advTask)==reinterpret_cast<void*>(advTask[9])); }
static void AdvPredicate() { CHECK(LoadAdvScriptTest(object)==advBusy); }
static void AdvEnd() { CHECK(LoadAdvCancelTest()==advCancel); }
static void AdvTests() {
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    SetAdvTimingTest(reinterpret_cast<void*(__thiscall*)(void*,void*)>(OriginalAdvData),OriginalAdvBusy,OriginalAdvCancel);
    advTask[9]=reinterpret_cast<uintptr_t>(advObject); advObject[0]=reinterpret_cast<uintptr_t>(advTask);
    advObject[4]=42; advObject[0x375c/4]=3;
    AdvBegin(); AdvPredicate(); CHECK(ReadLoadTimingTest().reason==0); AdvEnd();
    CHECK(ReadLoadTimingTest().reason==4 && advDataCalls==1 && advBusyCalls==1 && advCancelCalls==1);
    LoadClockTest(clockFields); CHECK(seenInterval==0 && clockFields[8]==16666);
    AdvBegin(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0); // No predicate evidence.
    advBusy=0x12340000; AdvBegin(); AdvPredicate(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    advBusy=0x12340001;
    for(auto phase : {0u,1u,2u,4u,5u,6u,7u,8u}) {
        advObject[0x375c/4]=phase; AdvBegin(); AdvPredicate(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    }
    advObject[0x375c/4]=3;
    AdvBegin(); AdvPredicate(); advObject[0x375c/4]=7; AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    advObject[0x375c/4]=3;
    AdvBegin(); AdvPredicate(); ++advObject[4]; AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    AdvBegin(); AdvPredicate(); advTask[9]=reinterpret_cast<uintptr_t>(advOther); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    advTask[9]=reinterpret_cast<uintptr_t>(advObject);
    AdvBegin(); AdvPredicate(); AdvPredicate(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    AdvBegin(); AdvPredicate(); advCancel=0xabcd0001; AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    advCancel=0xabcd0000;
    AdvBegin(); AdvPredicate(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==4);
    AdvBegin(); // Creation/destruction/non-update stops exclusion without a tail call.
    CHECK(ReadLoadTimingTest().reason==0); LoadClockTest(clockFields); CHECK(seenInterval==16666);
    advTask[9]=0; AdvBegin(); AdvPredicate(); AdvEnd(); CHECK(ReadLoadTimingTest().reason==0);
    advTask[9]=1; AdvBegin(); AdvPredicate(); AdvEnd();
    CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault));
    LoadClockTest(clockFields); CHECK(seenInterval==16666);
}
static uint32_t worldTask[10]{}, worldObject[0x254/4]{}, worldGlobal[0x16208/4]{};
static void* worldGlobalPointer=worldGlobal;
static void* worldCallback=reinterpret_cast<void*>(0x12340000);
static uint32_t worldReadyResult=0xabcd0000, worldMapResult=0x12340001;
static uint32_t worldCharacterResult=0x23450000, worldBusyResult=0x34560001;
static unsigned worldReadyCalls=0, worldDataCalls=0, worldMapCalls=0, worldCharCalls=0, worldBusyCalls=0;
static unsigned mapCallCount=1, charCallCount=0;
static bool worldThrows=false, worldChanges=false;
static void* __fastcall OriginalWorldData(void* manager, void*, void* handle) {
    CHECK(manager==task && handle==worldTask && ReadLoadTimingTest().reason==0);
    ++worldDataCalls; return reinterpret_cast<void*>(worldTask[9]);
}
static uint32_t __cdecl OriginalWorldMap(void* p) { CHECK(p==worldGlobal); ++worldMapCalls; return worldMapResult; }
static uint32_t __cdecl OriginalWorldBusy(void* p,uint32_t copy) {
    CHECK(p==object && copy==0x12345601); ++worldBusyCalls; return worldBusyResult;
}
static uint32_t __cdecl OriginalWorldCharacter(void* p) {
    CHECK(p==object); ++worldCharCalls;
    CHECK(LoadWorldBusyTest(p,0x12345601)==worldBusyResult);
    return worldCharacterResult;
}
static uint32_t __cdecl OriginalWorldReady() {
    ++worldReadyCalls;
    for(unsigned i=0;i<mapCallCount;++i) CHECK(LoadWorldMapTest(worldGlobal)==worldMapResult);
    for(unsigned i=0;i<charCallCount;++i) CHECK(LoadWorldCharacterTest(object)==worldCharacterResult);
    if(worldChanges)worldObject[0]=2;
    if(worldThrows)RaiseException(0xe0420003,0,0,nullptr);
    return worldReadyResult;
}
static void WorldBegin() { CHECK(LoadWorldDataTest(task,worldTask)==reinterpret_cast<void*>(worldTask[9])); }
static void WorldTick() { WorldBegin(); CHECK(LoadWorldReadyTest()==worldReadyResult); }
static void ExceptionalWorld() {
    __try { LoadWorldReadyTest(); CHECK(false); }
    __except(GetExceptionCode()==0xe0420003?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void WorldTests() {
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    SetWorldTimingTest(reinterpret_cast<void*(__thiscall*)(void*,void*)>(OriginalWorldData),
        OriginalWorldReady,OriginalWorldMap,OriginalWorldCharacter,OriginalWorldBusy,
        &worldGlobalPointer,worldCallback);
    worldTask[9]=reinterpret_cast<uintptr_t>(worldObject);
    worldObject[0x164/4]=reinterpret_cast<uintptr_t>(worldTask);
    worldObject[0x20c/4]=reinterpret_cast<uintptr_t>(worldCallback);
    worldGlobal[0x16204/4]=reinterpret_cast<uintptr_t>(worldTask);
    worldObject[0]=1; WorldTick();
    CHECK(ReadLoadTimingTest().reason==8 && worldReadyCalls==1 && worldDataCalls==1 && worldMapCalls==1);
    LoadClockTest(clockFields); CHECK(seenInterval==0 && clockFields[8]==16666);
    WorldBegin(); CHECK(ReadLoadTimingTest().reason==0); // Close before any callback command/free.
    worldMapResult=0x12340000; WorldTick(); CHECK(ReadLoadTimingTest().reason==0);
    charCallCount=1; WorldTick();
    CHECK(ReadLoadTimingTest().reason==8 && worldCharCalls==1 && worldBusyCalls==1);
    worldBusyResult=0x34560000; WorldTick(); CHECK(ReadLoadTimingTest().reason==0); // Setup false lacks busy proof.
    worldBusyResult=0x34560001; worldCharacterResult=0x23450001;
    WorldTick(); CHECK(ReadLoadTimingTest().reason==0); // A ready character cannot lend old busy evidence.
    worldCharacterResult=0x23450000; worldReadyResult=0xabcd0001;
    WorldTick(); CHECK(ReadLoadTimingTest().reason==0); worldReadyResult=0xabcd0000;
    charCallCount=3; WorldTick(); CHECK(ReadLoadTimingTest().reason==0); charCallCount=1;
    for(auto phase : {0u,2u,3u,4u,9u}) {
        worldObject[0]=phase; WorldTick(); CHECK(ReadLoadTimingTest().reason==0);
    }
    worldObject[0]=1;
    for(auto field : {0x164u/4,0x20cu/4}) {
        const auto saved=worldObject[field]; worldObject[field]=0;
        WorldTick(); CHECK(ReadLoadTimingTest().reason==0); worldObject[field]=saved;
    }
    worldGlobal[0x16204/4]=0; WorldTick(); CHECK(ReadLoadTimingTest().reason==0);
    worldGlobal[0x16204/4]=reinterpret_cast<uintptr_t>(worldTask);
    for(auto count : {0u,2u}) { mapCallCount=count; WorldTick(); CHECK(ReadLoadTimingTest().reason==0); }
    mapCallCount=1; worldChanges=true; WorldTick(); CHECK(ReadLoadTimingTest().reason==0);
    worldChanges=false; worldObject[0]=1;
    WorldTick(); CHECK(ReadLoadTimingTest().reason==8);
    WorldBegin(); worldThrows=true; ExceptionalWorld(); worldThrows=false;
    CHECK(ReadLoadTimingTest().reason==0);
    CHECK(LoadWorldCharacterTest(object)==worldCharacterResult && ReadLoadTimingTest().reason==0);
    CHECK(LoadWorldReadyTest()==worldReadyResult && ReadLoadTimingTest().reason==0); // No reused identity.
    WorldTick(); CHECK(ReadLoadTimingTest().reason==8);
    worldTask[9]=0; WorldTick(); CHECK(ReadLoadTimingTest().reason==0);
    worldTask[9]=1; WorldTick(); CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault));
    LoadClockTest(clockFields); CHECK(seenInterval==16666);
}
static uint32_t titleTask[10]{}, titleObject[0x54/4]{}, titleWindow[0x70/4]{}, titleData[8]{}, titleMovie[2]{}, titleVideo[0x520/4]{}, titleDecoder[4]{};
static void* titleTaskRoot = titleTask;
static void* titleCallback = reinterpret_cast<void*>(0x51b6a0);
static unsigned titleCalls=0;
static uint32_t expectedTitleReason=16;
static bool titleThrows=false, titleNests=false, titleDeletes=false;
static uint32_t __cdecl OriginalTitleSetup(void* video) {
    CHECK(video==titleVideo); ++titleCalls;
    CHECK(ReadLoadTimingTest().reason==expectedTitleReason);
    if(titleNests) { titleNests=false; CHECK(LoadTitleSetupTest(video)==0xaabb0001); CHECK(ReadLoadTimingTest().reason==expectedTitleReason); }
    if(titleDeletes) { titleTaskRoot=nullptr; titleTask[9]=0; } // No post-call heap dereference.
    if(titleThrows)RaiseException(0xe0420004,0,0,nullptr);
    return 0xaabb0001;
}
static void ExceptionalTitle() {
    __try { LoadTitleSetupTest(titleVideo); CHECK(false); }
    __except(GetExceptionCode()==0xe0420004?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void TitleFixture() {
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    SetTitleTimingTest(OriginalTitleSetup,&titleTaskRoot,titleCallback);
    titleTaskRoot=titleTask; titleTask[9]=reinterpret_cast<uintptr_t>(titleObject);
    titleObject[0]=1; titleObject[0x30/4]=reinterpret_cast<uintptr_t>(titleTask);
    titleObject[0x48/4]=reinterpret_cast<uintptr_t>(titleCallback);
    titleObject[0x44/4]=reinterpret_cast<uintptr_t>(titleWindow);
    titleWindow[0x6c/4]=reinterpret_cast<uintptr_t>(titleData);
    titleData[0x10/4]=2; titleData[0xc/4]=reinterpret_cast<uintptr_t>(titleMovie);
    titleMovie[0]=reinterpret_cast<uintptr_t>(titleVideo);
    titleVideo[1]=reinterpret_cast<uintptr_t>(titleDecoder); titleDecoder[1]=2;
    titleVideo[0x78/4]=4; titleVideo[0x54/4]=1;
    titleVideo[0x58/4]=titleVideo[0x4d8/4]=titleVideo[0x4dc/4]=0;
    titleThrows=titleNests=titleDeletes=false; expectedTitleReason=16;
}
static void TitleTests() {
    TitleFixture(); CHECK(LoadTitleSetupTest(titleVideo)==0xaabb0001 && titleCalls==1);
    CHECK(ReadLoadTimingTest().reason==0 && ReadLoadTimingTest().transitions==2 && ReadLoadTimingTest().completedTicks>0);
    CHECK(ReadLoadTimingTest().coverage==63 && ReadLoadTimingTest().fpsApplications==0);
    titleNests=true; LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().reason==0);
    titleThrows=true; ExceptionalTitle(); titleThrows=false; CHECK(ReadLoadTimingTest().reason==0);
    LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().reason==0); // Exception restores recursion guard.
    titleDeletes=true; LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().reason==0);
    TitleFixture(); expectedTitleReason=0; titleTaskRoot=nullptr;
    LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().completedTicks==0); titleTaskRoot=titleTask;
    for(auto phase : {0u,2u,3u,4u,5u,6u,7u}) {
        titleObject[0]=phase; LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().completedTicks==0);
    }
    titleObject[0]=1;
    for(auto* field : {&titleTask[9],&titleObject[0x30/4],&titleObject[0x48/4],&titleObject[0x44/4],
                      &titleWindow[0x6c/4],&titleData[0x10/4],&titleData[0xc/4],&titleMovie[0],
                      &titleVideo[1],&titleVideo[0x78/4],&titleVideo[0x54/4],&titleDecoder[1]}) {
        auto saved=*field; *field=0; LoadTitleSetupTest(titleVideo);
        CHECK(ReadLoadTimingTest().completedTicks==0); *field=saved;
    }
    titleMovie[0]=reinterpret_cast<uintptr_t>(object); LoadTitleSetupTest(titleVideo);
    CHECK(ReadLoadTimingTest().completedTicks==0); titleMovie[0]=reinterpret_cast<uintptr_t>(titleVideo);
    for(auto field : {0x58/4,0x4d8/4,0x4dc/4}) {
        titleVideo[field]=1; LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().completedTicks==0); titleVideo[field]=0;
    }
    titleVideo[0x78/4]=1; LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().completedTicks==0); // First-frame wait retained.
    titleVideo[0x78/4]=4; titleTask[9]=1; LoadTitleSetupTest(titleVideo);
    CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault));
    TitleFixture(); task[9]=reinterpret_cast<uintptr_t>(object); object[1]=nextPhase=2; expectedTitleReason=17;
    LoadBattleTest(77,2,task); LoadTitleSetupTest(titleVideo); CHECK(ReadLoadTimingTest().reason==1);
    LoadBattleTest(77,1,task); CHECK(ReadLoadTimingTest().reason==0);
}

static uint32_t saveTestMode=0,saveTestState=1,saveTestPhase=0,saveExpected=32,saveBaseReason=0;
static void* saveTestList=nullptr;
static unsigned saveIoCalls=0,saveScanCalls=0,saveNesting=0;
static bool saveThrows=false,saveChanges=false,saveRecursiveIo=false;
static uint32_t __cdecl OriginalSaveExists(uint32_t path) {
    CHECK(path==0x12345678); ++saveIoCalls; CHECK(ReadLoadTimingTest().reason==saveExpected);
    if(saveRecursiveIo) {saveRecursiveIo=false;CHECK(LoadSaveExistsTest(path)==0xaabbccdd);CHECK(ReadLoadTimingTest().reason==saveExpected);}
    if(saveThrows)RaiseException(0xe0420005,0,0,nullptr);
    return 0xaabbccdd;
}
static uint32_t __cdecl OriginalSaveOpen(uint32_t path,uint32_t flags,uint32_t mode) {
    CHECK(path==0x12345678 && flags==0x601 && mode==0); ++saveIoCalls;
    CHECK(ReadLoadTimingTest().reason==saveExpected); return 0x89abcdef;
}
static uint32_t __cdecl OriginalSaveRead(uint32_t handle,uint32_t buffer,uint32_t bytes) {
    CHECK(handle==0x89abcdef && buffer==0xfedcba98 && bytes==0x530); ++saveIoCalls;
    CHECK(ReadLoadTimingTest().reason==saveExpected); return 0xfedc0530;
}
static uint32_t __cdecl OriginalSaveClose(uint32_t handle) {
    CHECK(handle==0x89abcdef); ++saveIoCalls; CHECK(ReadLoadTimingTest().reason==saveExpected);
    return 0xabcdef00;
}
static void __cdecl OriginalSaveScan() {
    ++saveScanCalls; CHECK(ReadLoadTimingTest().reason==saveBaseReason);
    if(saveNesting) {
        --saveNesting; auto expected=saveExpected; saveExpected=saveBaseReason;
        LoadSaveScanTest(); saveExpected=expected;
        CHECK(ReadLoadTimingTest().reason==saveBaseReason);
    }
    CHECK(LoadSaveExistsTest(0x12345678)==0xaabbccdd);
    CHECK(ReadLoadTimingTest().reason==saveBaseReason); // Scan formatting/logging excluded from reason.
    if(saveChanges) {saveTestPhase=1;saveExpected=saveBaseReason;}
    CHECK(LoadSaveOpenTest(0x12345678,0x601,0)==0x89abcdef);
    CHECK(ReadLoadTimingTest().reason==saveBaseReason);
    CHECK(LoadSaveReadTest(0x89abcdef,0xfedcba98,0x530)==0xfedc0530);
    CHECK(ReadLoadTimingTest().reason==saveBaseReason);
    CHECK(LoadSaveCloseTest(0x89abcdef)==0xabcdef00);
    CHECK(ReadLoadTimingTest().reason==saveBaseReason);
}
static void SaveFixture() {
    ResetLoadTimingTest(OriginalBattle,OriginalClock,true,false);
    saveTestMode=saveTestPhase=saveBaseReason=saveNesting=0;saveTestState=1;saveExpected=32;saveTestList=nullptr;
    saveThrows=saveChanges=saveRecursiveIo=false;saveIoCalls=saveScanCalls=0;
    SetSaveTimingTest(OriginalSaveScan,OriginalSaveExists,OriginalSaveOpen,OriginalSaveRead,OriginalSaveClose,
                      &saveTestMode,&saveTestState,&saveTestPhase,&saveTestList);
}
static void ExceptionalSave() {
    __try {LoadSaveScanTest();CHECK(false);}
    __except(GetExceptionCode()==0xe0420005?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void SaveTests() {
    SaveFixture();LoadSaveScanTest();
    CHECK(saveScanCalls==1 && saveIoCalls==4 && ReadLoadTimingTest().transitions==8);
    CHECK(ReadLoadTimingTest().completedTicks>0 && ReadLoadTimingTest().reason==0);
    LoadClockTest(clockFields); CHECK(seenInterval==16666 && ReadLoadTimingTest().fpsApplications==0);
    SaveFixture();saveExpected=0;LoadSaveExistsTest(0x12345678);LoadSaveOpenTest(0x12345678,0x601,0);
    LoadSaveReadTest(0x89abcdef,0xfedcba98,0x530);LoadSaveCloseTest(0x89abcdef);
    CHECK(ReadLoadTimingTest().completedTicks==0); // Same wrappers outside create scan do not qualify.
    for(auto mode : {1u,2u,0xffffffffu}) {SaveFixture();saveTestMode=mode;saveExpected=0;LoadSaveScanTest();CHECK(ReadLoadTimingTest().completedTicks==0);}
    for(auto state : {0u,2u,3u}) {SaveFixture();saveTestState=state;saveExpected=0;LoadSaveScanTest();CHECK(ReadLoadTimingTest().completedTicks==0);}
    for(auto phase : {1u,2u,0xffffffffu}) {SaveFixture();saveTestPhase=phase;saveExpected=0;LoadSaveScanTest();CHECK(ReadLoadTimingTest().completedTicks==0);}
    SaveFixture();saveTestList=object;saveExpected=0;LoadSaveScanTest();CHECK(ReadLoadTimingTest().completedTicks==0);
    SaveFixture();saveNesting=2;LoadSaveScanTest();CHECK(saveScanCalls==3 && ReadLoadTimingTest().transitions==8);
    SaveFixture();saveRecursiveIo=true;LoadSaveScanTest();CHECK(saveIoCalls==5 && ReadLoadTimingTest().transitions==8);
    SaveFixture();saveChanges=true;LoadSaveScanTest();CHECK(ReadLoadTimingTest().transitions==2 && ReadLoadTimingTest().reason==0);
    SaveFixture();saveThrows=true;ExceptionalSave();CHECK(ReadLoadTimingTest().reason==0);
    saveThrows=false;LoadSaveScanTest();CHECK(ReadLoadTimingTest().transitions==10); // Both guards restored.
    SaveFixture();saveExpected=0;
    SetSaveTimingTest(OriginalSaveScan,OriginalSaveExists,OriginalSaveOpen,OriginalSaveRead,OriginalSaveClose,
                      reinterpret_cast<uint32_t*>(1),&saveTestState,&saveTestPhase,&saveTestList);
    LoadSaveScanTest();CHECK(ReadLoadTimingTest().status==static_cast<uint32_t>(LoadStatus::Fault) && saveIoCalls==4);
    SaveFixture();task[9]=reinterpret_cast<uintptr_t>(object);object[1]=nextPhase=2;
    LoadBattleTest(77,2,task);saveBaseReason=1;saveExpected=33;LoadSaveScanTest();CHECK(ReadLoadTimingTest().reason==1);
    LoadBattleTest(77,1,task);CHECK(ReadLoadTimingTest().reason==0);
}

int main() {
    CoreTests(); AdapterTests(); DungeonTests(); AdvTests(); WorldTests(); TitleTests(); SaveTests();
    std::printf("PASS: %u checks; interval union, lifetime/focus/faults, sub-poll loads, x86 callback and clock restoration\n",checks);
}
