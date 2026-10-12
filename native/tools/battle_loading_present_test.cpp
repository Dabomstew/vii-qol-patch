#include "battle_loading_present.hpp"
#include "load_timing.hpp"
#include "code_calls.hpp"
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
namespace vii {
static unsigned logs = 0;
void Log(const char*, ...) noexcept { ++logs; }
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks = 0, realCalls = 0, resources = 0, callbackCalls = 0;
static UINT lastSync = 0, lastFlags = 0;
static HRESULT nextResult = S_OK, descResult = S_OK;
static DXGI_SWAP_EFFECT effect = DXGI_SWAP_EFFECT_DISCARD;
static uint32_t phaseAfter = 1;
static bool nestedPresent = false, raiseCallback = false;
static IDXGISwapChain* lastChain = nullptr;
static void* slots[18]{};
struct Chain { void** table = slots; } chainA, chainB;
static unsigned char renderer[0x80]{};
static void* rendererPointer = renderer;
static uint32_t task[10]{}, object[16]{};
static void* siteCode = nullptr;
static DWORD comparisonFlags = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(false)
static IDXGISwapChain* ChainA() { return reinterpret_cast<IDXGISwapChain*>(&chainA); }
static HRESULT __stdcall Original(IDXGISwapChain* chain, UINT sync, UINT flags) {
    ++realCalls; lastChain = chain; lastSync = sync; lastFlags = flags; return nextResult;
}
static HRESULT __stdcall Desc(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC* desc) {
    *desc = {}; desc->SwapEffect = effect; return descResult;
}
static uint32_t __cdecl Callback(uint32_t command, uint32_t mode, void* owner) {
    ++callbackCalls;
    CHECK(command == 0x12345678 && owner == task);
    if (raiseCallback) RaiseException(0xe0123456, 0, 0, nullptr);
    if (nestedPresent) CHECK(BattlePresentTest(ChainA(), 0, 0) == nextResult);
    if (mode == 2) object[1] = phaseAfter;
    return 0x89abcdef;
}
static void __fastcall Clock(void*) {}
static void Poll(uint32_t next, uint32_t mode = 2) {
    phaseAfter = next;
    CHECK(LoadBattleTest(0x12345678, mode, task) == 0x89abcdef);
}
static void Reset(bool active = true, bool diagnostic = false) {
    slots[8] = reinterpret_cast<void*>(Original); slots[12] = reinterpret_cast<void*>(Desc);
    *reinterpret_cast<IDXGISwapChain**>(renderer + 0x34) = ChainA();
    *reinterpret_cast<uint32_t*>(renderer + 0x7c) = 0;
    task[9] = reinterpret_cast<uint32_t>(object); object[1] = 0;
    realCalls = resources = callbackCalls = logs = 0;
    nextResult = descResult = S_OK; effect = DXGI_SWAP_EFFECT_DISCARD;
    nestedPresent = raiseCallback = false;
    SetBattlePresentClockTest(1000, true);
    ResetBattlePresentTest(&rendererPointer, active, diagnostic);
    ResetLoadTimingTest(Callback, Clock, false, false);
}
static HRESULT Drain(UINT sync = 0, UINT flags = 0) {
    ++resources; // Required resource work must run even when Present is skipped.
    return BattlePresentTest(ChainA(), sync, flags);
}
static void Arm() {
    Poll(1); CHECK(Drain() == S_OK && realCalls == 1);
    CHECK(Drain() == S_OK && realCalls == 1 && resources == 2);
}
static bool CatchCallbackException() {
    __try { LoadBattleTest(0x12345678,2,task); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static void LifecycleTests() {
    Reset(false); Poll(1); CHECK(Drain() == S_OK && Drain() == S_OK && realCalls == 2);
    Reset(); CHECK(Drain() == S_OK && realCalls == 1); Reset(); Arm();
    Poll(2); CHECK(Drain() == S_OK && realCalls == 1);
    Poll(2); CHECK(Drain() == S_OK && realCalls == 1); // Real between-polls ordering.
    Poll(3); CHECK(Drain() == S_OK && realCalls == 1);
    Poll(4); CHECK(Drain() == S_OK && realCalls == 2);
    Poll(4); CHECK(Drain() == S_OK && realCalls == 3 && logs == 0);
    Reset(); Arm(); SetBattlePresentClockTest(1250, true);
    CHECK(Drain() == S_OK && realCalls == 2); CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); SetBattlePresentClockTest(16000, true);
    CHECK(Drain() == S_OK && realCalls == 2); CHECK(Drain() == S_OK && realCalls == 3);
    Reset(); Arm(); SetBattlePresentClockTest(1001, false);
    CHECK(Drain() == S_OK && realCalls == 2); SetBattlePresentClockTest(1002, true);
    CHECK(Drain() == S_OK && realCalls == 3); // Focus regain cannot resurrect evidence.
    Reset(); Arm(); Poll(2, 1); CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); Poll(2, 0); CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); nestedPresent = true; Poll(2); nestedPresent = false;
    CHECK(realCalls == 2); CHECK(Drain() == S_OK && realCalls == 2); // Native during active poll.
    Reset(); Arm(); object[1] = 4; CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); object[1] = 0; CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); task[9] = 1; CHECK(Drain() == S_OK && realCalls == 2); // Guarded unreadable owner.
    Reset(); Arm(); task[9] = 0; CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); std::thread other([] { CHECK(Drain() == S_OK); }); other.join();
    CHECK(realCalls == 2); CHECK(Drain() == S_OK && realCalls == 3);
    Reset(); Arm(); auto a = BeginBattleLoadingPresent(task,2); auto b = BeginBattleLoadingPresent(task,2);
    EndBattleLoadingPresent(b,true); EndBattleLoadingPresent(a,true);
    CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); raiseCallback = true;
    CHECK(CatchCallbackException()); raiseCallback = false; CHECK(Drain() == S_OK && realCalls == 2);
    Reset(); Arm(); for (unsigned i = 1; i < 20000; ++i) CHECK(Drain() == S_OK);
    CHECK(realCalls == 1); CHECK(Drain() == S_OK && realCalls == 2);
    Reset(true,true); Arm(); Poll(4); CHECK(logs == 1);
}
static void PassthroughTests() {
    for (auto result : {E_FAIL, DXGI_ERROR_DEVICE_REMOVED, DXGI_STATUS_OCCLUDED}) {
        Reset(); Arm(); nextResult = result; SetBattlePresentClockTest(1250,true);
        CHECK(Drain() == result && realCalls == 2);
        SetBattlePresentClockTest(1251,true); CHECK(Drain() == result && realCalls == 3);
    }
    Reset(); Arm(); CHECK(Drain(1,0) == S_OK && realCalls == 2 && lastSync == 1 && lastFlags == 0);
    Reset(); Arm(); CHECK(Drain(0,DXGI_PRESENT_TEST) == S_OK && realCalls == 2 && lastFlags == DXGI_PRESENT_TEST);
    Reset(); Arm(); auto* second = reinterpret_cast<IDXGISwapChain*>(&chainB);
    CHECK(BattlePresentTest(second,0,0) == S_OK && lastChain == second && realCalls == 2);
    Reset(); effect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; Poll(1); Drain(); Drain(); CHECK(realCalls == 2);
    Reset(); descResult = E_FAIL; Poll(1); Drain(); Drain(); CHECK(realCalls == 2);
    Reset(); Arm(); *reinterpret_cast<uint32_t*>(renderer+0x7c) = 1; Drain(); Drain(); CHECK(realCalls == 3);
}
// This shim reproduces the original argument pushes and saved ESI. JMP avoids
// inserting an extra return address at the site's stdcall argument boundary.
static __declspec(naked) HRESULT __stdcall InvokeSite(IDXGISwapChain*,UINT,UINT) {
    __asm {
        push esi
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        mov eax, dword ptr [esp]
        mov ecx, dword ptr [eax]
        jmp dword ptr [siteCode]
    }
}
static void MachineSiteTests() {
    auto* bytes = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(bytes != nullptr); siteCode = bytes;
    const unsigned char prefix[]{0xff,0x51,0x20,0x5e,0x3d,0x01,0x00,0x7a,0x08};
    std::memcpy(bytes,prefix,sizeof(prefix));
    const unsigned char tail[]{0x9c,0x8f,0x05,0,0,0,0,0xc2,0x0c,0x00};
    std::memcpy(bytes+9,tail,sizeof(tail)); auto flags = reinterpret_cast<uint32_t>(&comparisonFlags);
    std::memcpy(bytes+12,&flags,4); DWORD old = 0;
    CHECK(VirtualProtect(bytes,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),bytes,19));
    Reset(false); CHECK(InvokeSite(ChainA(),2,7) == S_OK && lastSync == 2 && lastFlags == 7);
    SetBattlePresentResumeTest(bytes+9); std::array<unsigned char,8> expected{};
    std::memcpy(expected.data(),prefix,8);
    SetCallFault(CallFault::AfterFirstWrite);
    auto report = ReplaceEntryJump({bytes,expected,BattlePresentEntryTest()});
    CHECK(!report.installed && report.rolledBack && !std::memcmp(bytes,prefix,9));
    CHECK(InvokeSite(ChainA(),0,0) == S_OK);
    report = ReplaceEntryJump({bytes,expected,BattlePresentEntryTest()}); CHECK(report.installed);
    unsigned before = 0, after = 0, esiBefore = 0, esiAfter = 0;
    __asm mov before,esp
    __asm mov esiBefore,esi
    nextResult = DXGI_STATUS_OCCLUDED;
    CHECK(InvokeSite(ChainA(),3,9) == nextResult && lastSync == 3 && lastFlags == 9 && (comparisonFlags & 0x40));
    nextResult = E_FAIL; CHECK(InvokeSite(ChainA(),0,0) == E_FAIL && !(comparisonFlags & 0x40));
    __asm mov after,esp
    __asm mov esiAfter,esi
    CHECK(before == after && esiBefore == esiAfter);
    Reset(); Poll(1); CHECK(InvokeSite(ChainA(),0,0) == S_OK && realCalls == 1);
    CHECK(InvokeSite(ChainA(),0,0) == S_OK && realCalls == 1 && !(comparisonFlags & 0x40));
    Poll(4); CHECK(InvokeSite(ChainA(),0,0) == S_OK && realCalls == 2);
    VirtualFree(bytes,0,MEM_RELEASE);
}
static void WarpTest() {
    HWND window = CreateWindowExW(0,L"STATIC",L"VII native Present test",WS_OVERLAPPEDWINDOW,
        0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(window != nullptr);
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferDesc.Width = desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1;
    desc.OutputWindow = window; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* chain = nullptr; ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr;
    CHECK(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)));
    Reset(); *reinterpret_cast<IDXGISwapChain**>(renderer+0x34) = chain; Poll(1);
    // Hidden real WARP window: no focus changes. Preserve native occlusion result.
    const auto baseline = chain->Present(0,0);
    CHECK(BattlePresentTest(chain,0,0) == baseline);
    if (baseline == S_OK) CHECK(BattlePresentTest(chain,0,0) == S_OK);
    Poll(4); CHECK(BattlePresentTest(chain,0,0) == chain->Present(0,0));
    context->ClearState(); context->Flush(); context->Release(); device->Release(); chain->Release();
    DestroyWindow(window);
}
int main() {
    LifecycleTests(); PassthroughTests(); MachineSiteTests(); WarpTest();
    std::printf("PASS: %u checks; original callback/resource work, between-polls guards, real heartbeat/resume, HRESULT/ESP/ESI/flags, WARP\n",checks);
}
