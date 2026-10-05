#include "neptasm.hpp"
#include "code_calls.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace vii {
namespace {
struct Settings {
    bool fps = false, camera = false, resolution = false, fitWindow = false;
    bool windowControl = false, ultrawide = false;
    unsigned windowWidth = 1920, windowHeight = 1080;
    float scale = 1.0f;
};
struct State {
    Context context;
    Settings settings;
    HMODULE d3d11 = nullptr;
    unsigned sourceWidth = 1920, sourceHeight = 1080; // stock render-target dimensions
    unsigned outputWidth = 1920, outputHeight = 1080;
    unsigned renderWidth = 1920, renderHeight = 1080;
    unsigned uiWidth = 1920, uiHeight = 1080;
    float aspect = 1920.0f / 1080.0f;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* contextObject = nullptr;
    ID3D11DeviceContext* originalContext = nullptr;
    ID3D11Device* originalDevice = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    void** deviceVtable = nullptr;
    void** contextVtable = nullptr;
    void** swapVtable = nullptr;
};
State* state = nullptr; // pinned for process lifetime; COM objects own the vtables.

using CreateDeviceFn = HRESULT (WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using CreateTextureFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using CreateRtvFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*, ID3D11RenderTargetView**);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using GetBufferFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, REFIID, void**);
using OmSetFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*);
using ViewportFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
using ClearRtvFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);

CreateDeviceFn originalCreate = nullptr;
CreateTextureFn originalTexture = nullptr;
CreateRtvFn originalRtv = nullptr;
PresentFn originalPresent = nullptr;
GetBufferFn originalGetBuffer = nullptr;
OmSetFn originalOmSet = nullptr;
ViewportFn originalViewport = nullptr;
ClearRtvFn originalClear = nullptr;
float fpsMax = 1024.0f;
uintptr_t fpsReturn = 0;
float aspectRatio = 16.0f / 9.0f;
bool aspectPatched = false;

extern "C" __declspec(naked) void FpsFrameGate() {
    __asm {
        xor edi, edi
        test edi, edi
        jmp dword ptr [fpsReturn]
    }
}

bool InGame(uintptr_t caller) noexcept {
    if (!state || !state->context.game) return false;
    const auto base = reinterpret_cast<uintptr_t>(state->context.game);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    __try {
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE && caller >= base && caller < base + nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned ClampDimension(float value, unsigned fallback) {
    if (!std::isfinite(value) || value < 1.0f || value > 16384.0f) return fallback;
    return static_cast<unsigned>(value + 0.5f);
}
void Recompute() {
    auto& s = *state;
    const unsigned w = s.settings.fitWindow ? (s.outputWidth ? s.outputWidth : 1920) :
        (s.settings.windowControl ? s.settings.windowWidth : (s.outputWidth ? s.outputWidth : 1920));
    const unsigned h = s.settings.fitWindow ? (s.outputHeight ? s.outputHeight : 1080) :
        (s.settings.windowControl ? s.settings.windowHeight : (s.outputHeight ? s.outputHeight : 1080));
    const bool changeRender = s.settings.resolution || s.settings.fitWindow || s.settings.ultrawide;
    s.renderWidth = changeRender ? ClampDimension(float(w) * (s.settings.resolution ? s.settings.scale : 1.0f), w) : s.sourceWidth;
    s.renderHeight = changeRender ? ClampDimension(float(h) * (s.settings.resolution ? s.settings.scale : 1.0f), h) : s.sourceHeight;
    s.uiWidth = s.renderWidth;
    s.uiHeight = s.renderHeight;
    s.aspect = s.renderHeight ? float(s.renderWidth) / float(s.renderHeight) : 16.0f / 9.0f;
}
template<class T> T Original(void** table, unsigned slot) { return reinterpret_cast<T>(table[slot]); }
void** CloneVtable(IUnknown* object, size_t entries) {
    auto table = static_cast<void**>(VirtualAlloc(nullptr, entries * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!table) return nullptr;
    std::memcpy(table, *reinterpret_cast<void***>(object), entries * sizeof(void*));
    *reinterpret_cast<void***>(object) = table;
    return table;
}
HRESULT STDMETHODCALLTYPE CreateTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* desc,
    const D3D11_SUBRESOURCE_DATA* initial, ID3D11Texture2D** output) {
    if (state && (state->settings.resolution || state->settings.fitWindow || state->settings.ultrawide) && InGame(reinterpret_cast<uintptr_t>(_ReturnAddress())) && desc && !initial && desc->Width == state->sourceWidth &&
        desc->Height == state->sourceHeight && desc->SampleDesc.Count == 1 && desc->Usage != D3D11_USAGE_DYNAMIC &&
        (desc->Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc->Format == DXGI_FORMAT_D32_FLOAT || desc->Format == DXGI_FORMAT_R32_FLOAT)) {
        auto changed = *desc;
        changed.Width = state->renderWidth; changed.Height = state->renderHeight;
        return originalTexture(device, &changed, initial, output);
    }
    return originalTexture(device, desc, initial, output);
}
HRESULT STDMETHODCALLTYPE CreateRtv(ID3D11Device* device, ID3D11Resource* resource,
    const D3D11_RENDER_TARGET_VIEW_DESC* desc, ID3D11RenderTargetView** output) {
    return originalRtv(device, resource, desc, output);
}
HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain, UINT sync, UINT flags) {
    return originalPresent(chain, sync, flags);
}
HRESULT STDMETHODCALLTYPE GetBuffer(IDXGISwapChain* chain, UINT index, REFIID iid, void** output) {
    return originalGetBuffer(chain, index, iid, output);
}
void STDMETHODCALLTYPE OmSet(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views) {
    originalOmSet(context, count, views);
}
void STDMETHODCALLTYPE Viewport(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports) {
    if (!state || !(state->settings.resolution || state->settings.fitWindow || state->settings.ultrawide) ||
        !InGame(reinterpret_cast<uintptr_t>(_ReturnAddress())) || count != 1 || !viewports) {
        originalViewport(context, count, viewports); return;
    }
    auto viewport = *viewports;
    if (std::abs(viewport.Width - float(state->sourceWidth)) < 0.5f &&
        std::abs(viewport.Height - float(state->sourceHeight)) < 0.5f) {
        viewport.Width = float(state->renderWidth); viewport.Height = float(state->renderHeight);
    } else if (viewport.TopLeftX != 0.0f || viewport.TopLeftY != 0.0f) {
        // neptasm scales the game's secondary/UI viewport coordinates along
        // with the render target. Use independent axes for non-16:9 output;
        // this preserves the same behavior for both up/down-scaling and
        // ultrawide output while leaving origin-anchored special passes alone.
        const float sx = float(state->renderWidth) / float(state->sourceWidth);
        const float sy = float(state->renderHeight) / float(state->sourceHeight);
        viewport.TopLeftX *= sx; viewport.Width *= sx;
        viewport.TopLeftY *= sy; viewport.Height *= sy;
    }
    originalViewport(context, count, &viewport);
}
void STDMETHODCALLTYPE ClearRtv(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]) {
    originalClear(context, view, color);
}
bool PatchAspect(const Context& context, float ratio);

bool InstallComHooks(IDXGISwapChain* chain, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!chain || !device || !context) return false;
    state->swapChain = chain; state->device = device; state->contextObject = context;
    state->sourceWidth = 1920; state->sourceHeight = 1080;
    DXGI_SWAP_CHAIN_DESC desc{}; if (SUCCEEDED(chain->GetDesc(&desc)) && desc.BufferDesc.Width && desc.BufferDesc.Height) {
        state->outputWidth = desc.BufferDesc.Width; state->outputHeight = desc.BufferDesc.Height;
    }
    Recompute();
    state->swapVtable = CloneVtable(chain, 18); state->deviceVtable = CloneVtable(device, 43); state->contextVtable = CloneVtable(context, 51);
    if (!state->swapVtable || !state->deviceVtable || !state->contextVtable) return false;
    originalPresent = Original<PresentFn>(state->swapVtable, 8); originalGetBuffer = Original<GetBufferFn>(state->swapVtable, 9);
    originalTexture = Original<CreateTextureFn>(state->deviceVtable, 5); originalRtv = Original<CreateRtvFn>(state->deviceVtable, 9);
    originalOmSet = Original<OmSetFn>(state->contextVtable, 33); originalViewport = Original<ViewportFn>(state->contextVtable, 44); originalClear = Original<ClearRtvFn>(state->contextVtable, 50);
    state->swapVtable[8] = reinterpret_cast<void*>(Present); state->swapVtable[9] = reinterpret_cast<void*>(GetBuffer);
    state->deviceVtable[5] = reinterpret_cast<void*>(CreateTexture); state->deviceVtable[9] = reinterpret_cast<void*>(CreateRtv);
    state->contextVtable[33] = reinterpret_cast<void*>(OmSet); state->contextVtable[44] = reinterpret_cast<void*>(Viewport); state->contextVtable[50] = reinterpret_cast<void*>(ClearRtv);
    DWORD ignored = 0;
    VirtualProtect(state->swapVtable, 18 * sizeof(void*), PAGE_READONLY, &ignored);
    VirtualProtect(state->deviceVtable, 43 * sizeof(void*), PAGE_READONLY, &ignored);
    VirtualProtect(state->contextVtable, 51 * sizeof(void*), PAGE_READONLY, &ignored);
    return originalPresent && originalGetBuffer && originalTexture && originalRtv && originalOmSet && originalViewport && originalClear;
}

HRESULT WINAPI CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk, const DXGI_SWAP_CHAIN_DESC* swapDesc,
    IDXGISwapChain** chain, ID3D11Device** device, D3D_FEATURE_LEVEL* feature, ID3D11DeviceContext** context) {
    DXGI_SWAP_CHAIN_DESC adjusted{};
    const DXGI_SWAP_CHAIN_DESC* effective = swapDesc;
    if (state && state->settings.fitWindow && swapDesc && swapDesc->OutputWindow) {
        adjusted = *swapDesc; RECT client{};
        if (GetClientRect(swapDesc->OutputWindow, &client) && client.right > client.left && client.bottom > client.top) {
            adjusted.BufferDesc.Width = static_cast<UINT>(client.right - client.left);
            adjusted.BufferDesc.Height = static_cast<UINT>(client.bottom - client.top);
            effective = &adjusted;
        }
    }
    const bool runtimeAspect = state && (state->settings.fitWindow || state->settings.ultrawide);
    const float effectiveAspect = effective && effective->BufferDesc.Height
        ? float(effective->BufferDesc.Width) / float(effective->BufferDesc.Height) : 16.0f / 9.0f;
    if (runtimeAspect && !aspectPatched && effective && effective->BufferDesc.Height &&
        std::abs(effectiveAspect - 16.0f / 9.0f) > 0.0001f)
        if (!PatchAspect(state->context, effectiveAspect))
            Log("Neptasm aspect-ratio signature mismatch; retaining stock projection");
    auto result = originalCreate(adapter, type, software, flags, levels, levelCount, sdk, effective, chain, device, feature, context);
    if (SUCCEEDED(result) && state && (state->settings.resolution || state->settings.ultrawide || state->settings.fitWindow) && chain && *chain && device && context)
        if (!InstallComHooks(*chain, *device, *context)) Log("Neptasm COM hook installation failed; preserving original rendering");
    return result;
}

bool PatchBytes(uintptr_t address, const unsigned char* expected, const unsigned char* replacement, size_t size) {
    if (std::memcmp(reinterpret_cast<void*>(address), expected, size)) return false;
    DWORD old = 0; if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(reinterpret_cast<void*>(address), replacement, size); FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    DWORD ignored = 0; VirtualProtect(reinterpret_cast<void*>(address), size, old, &ignored); return true;
}
bool PatchDwordExpected(uintptr_t address, uint32_t expectedValue, uint32_t value) {
    unsigned char expected[4], replacement[4]; std::memcpy(expected, &expectedValue, 4); std::memcpy(replacement, &value, 4);
    return PatchBytes(address, expected, replacement, 4);
}
bool PatchAspect(const Context& context, float ratio) {
    if (aspectPatched) return true;
    if (!std::isfinite(ratio) || ratio < 0.25f || ratio > 8.0f) return false;
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    const unsigned char projectionExpected[] = {0x86,0x80,0x00,0x00,0x00};
    unsigned char projectionReplacement[] = {0x05,0,0,0,0};
    const uint32_t aspectPointer = uint32_t(reinterpret_cast<uintptr_t>(&aspectRatio));
    std::memcpy(projectionReplacement + 1, &aspectPointer, 4);
    const uint32_t priorMapBits = 0x3fe38e39;
    if (std::memcmp(reinterpret_cast<void*>(base + 0x1081e1), projectionExpected, sizeof(projectionExpected)) ||
        std::memcmp(reinterpret_cast<void*>(base + 0x5bdc94), &priorMapBits, sizeof(priorMapBits))) return false;
    aspectRatio = ratio; std::memcpy(projectionReplacement + 1, &aspectPointer, 4);
    if (!PatchBytes(base + 0x1081e1, projectionExpected, projectionReplacement, sizeof(projectionExpected))) return false;
    if (!PatchBytes(base + 0x5bdc94, reinterpret_cast<const unsigned char*>(&priorMapBits), reinterpret_cast<const unsigned char*>(&aspectRatio), sizeof(priorMapBits))) return false;
    aspectPatched = true; return true;
}
bool WindowTableMatches(uintptr_t base) {
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(base + 0x6f04c8);
    for (unsigned i = 0; i < 10; ++i) {
        uint32_t width = 0, height = 0; std::memcpy(&width, raw + i * 8, 4); std::memcpy(&height, raw + i * 8 + 4, 4);
        if (!((width == 640 && height == 360) || (width == 720 && height == 405) || (width == 800 && height == 450) ||
            (width == 1024 && height == 576) || (width == 1152 && height == 648) || (width == 1280 && height == 720) ||
            (width == 1360 && height == 765) || (width == 1366 && height == 768) || (width == 1600 && height == 900) || (width == 1920 && height == 1080))) return false;
    }
    return true;
}
bool PatchWindowTable(const Settings& s, uintptr_t base) {
    if (!WindowTableMatches(base)) return false;
    DWORD old = 0; if (!VirtualProtect(reinterpret_cast<void*>(base + 0x6f04c8), 80, PAGE_READWRITE, &old)) return false;
    for (unsigned i = 0; i < 10; ++i) { std::memcpy(reinterpret_cast<void*>(base + 0x6f04c8 + i * 8), &s.windowWidth, 4); std::memcpy(reinterpret_cast<void*>(base + 0x6f04c8 + i * 8 + 4), &s.windowHeight, 4); }
    DWORD ignored = 0; VirtualProtect(reinterpret_cast<void*>(base + 0x6f04c8), 80, old, &ignored); return true;
}
bool ApplyStatic(const Context& context, const Settings& s) {
    const auto base = reinterpret_cast<uintptr_t>(context.game); bool ok = true;
    // Validate every byte/dword for the selected static features before the
    // first write. This prevents an incompatible build from receiving a
    // partial FPS/camera/window install.
    if (s.windowControl && !WindowTableMatches(base)) return false;
    if (s.fps) {
        const unsigned char frameExpected[] = {0x8b,0x7e,0x20,0x85,0xff,0x0f,0x84,0x8a};
        const unsigned char skipExpected[] = {0x52,0x50};
        uint32_t maxPointer = 0; std::memcpy(&maxPointer, reinterpret_cast<void*>(base + 0x3b20a8), 4);
        if (std::memcmp(reinterpret_cast<void*>(base + 0x404b4c), frameExpected, sizeof(frameExpected)) ||
            std::memcmp(reinterpret_cast<void*>(base + 0x46bb12), skipExpected, sizeof(skipExpected)) ||
            maxPointer != uint32_t(base + 0x4f2b80)) return false;
    }
    if (s.camera) {
        const unsigned char maxExpected[] = {0x96,0xb4,0x01,0x00,0x00};
        const unsigned char minExpected[] = {0xae,0xb8,0x01,0x00,0x00};
        if (std::memcmp(reinterpret_cast<void*>(base + 0x2c147b), maxExpected, sizeof(maxExpected)) ||
            std::memcmp(reinterpret_cast<void*>(base + 0x2c1486), minExpected, sizeof(minExpected))) return false;
    }
    if (s.windowControl && std::abs(float(s.windowWidth) / float(s.windowHeight) - 16.0f / 9.0f) > 0.0001f) {
        const unsigned char projectionExpected[] = {0x86,0x80,0x00,0x00,0x00};
        const uint32_t mapExpected = 0x3fe38e39;
        if (std::memcmp(reinterpret_cast<void*>(base + 0x1081e1), projectionExpected, sizeof(projectionExpected)) ||
            std::memcmp(reinterpret_cast<void*>(base + 0x5bdc94), &mapExpected, sizeof(mapExpected))) return false;
    }
    if (s.windowControl) ok = PatchWindowTable(s, base) && ok;
    const float configuredAspect = float(s.windowWidth) / float(s.windowHeight);
    if (s.windowControl && std::abs(configuredAspect - 16.0f / 9.0f) > 0.0001f)
        ok = PatchAspect(context, configuredAspect) && ok;
    if (s.fps) {
        const unsigned char frameExpected[] = {0x8b,0x7e,0x20,0x85,0xff,0x0f,0x84,0x8a};
        fpsReturn = base + 0x404b51;
        CallSite frame{reinterpret_cast<unsigned char*>(base + 0x404b4c), {}, reinterpret_cast<void*>(FpsFrameGate)};
        std::memcpy(frame.expected.data(), frameExpected, sizeof(frameExpected));
        const auto report = ReplaceEntryJump(frame);
        ok = report.installed && report.status == CallStatus::Installed && ok;
        const unsigned char skipExpected[] = {0x52,0x50}, skipReplacement[] = {0xeb,0x05};
        ok = PatchBytes(base + 0x46bb12, skipExpected, skipReplacement, sizeof(skipExpected)) && ok;
        const uint32_t relocatedOriginal = uint32_t(base + 0x4f2b80);
        ok = PatchDwordExpected(base + 0x3b20a8, relocatedOriginal, uint32_t(reinterpret_cast<uintptr_t>(&fpsMax))) && ok;
    }
    if (s.camera) {
        const unsigned char maxExpected[] = {0x96,0xb4,0x01,0x00,0x00}; unsigned char maxReplacement[] = {0x15,0,0,0,0};
        const unsigned char minExpected[] = {0xae,0xb8,0x01,0x00,0x00}; unsigned char minReplacement[] = {0x2d,0,0,0,0};
        auto maxReplacementMutable = maxReplacement; auto minReplacementMutable = minReplacement;
        const float max = 270.0f, min = 90.0f; std::memcpy(maxReplacementMutable + 1, &max, 4); std::memcpy(minReplacementMutable + 1, &min, 4);
        ok = PatchBytes(base + 0x2c147b, maxExpected, maxReplacementMutable, sizeof(maxExpected)) && ok;
        ok = PatchBytes(base + 0x2c1486, minExpected, minReplacementMutable, sizeof(minExpected)) && ok;
    }
    return ok;
}
}

bool InstallNeptasm(const Context& context) {
    try {
        auto candidate = std::make_unique<State>(); candidate->context = context;
        auto& s = candidate->settings;
        s.fps = Option(context, L"Neptasm", L"FPSUnlock", 0) != 0;
        s.camera = Option(context, L"Neptasm", L"CameraUnlock", 0) != 0;
        s.resolution = Option(context, L"Neptasm", L"Resolution", 0) != 0;
        s.fitWindow = Option(context, L"Neptasm", L"FitWindow", 0) != 0;
        s.windowControl = Option(context, L"Neptasm", L"WindowControl", 0) != 0;
        s.ultrawide = Option(context, L"Neptasm", L"Ultrawide", 0) != 0;
        s.windowWidth = unsigned(std::clamp(Option(context, L"Neptasm", L"WindowWidth", 1920), 320, 16384));
        s.windowHeight = unsigned(std::clamp(Option(context, L"Neptasm", L"WindowHeight", 1080), 200, 16384));
        wchar_t scale[64]{}; GetPrivateProfileStringW(L"Neptasm", L"ResolutionScale", L"1.0", scale, 64, context.ini.c_str());
        wchar_t* end = nullptr; s.scale = wcstof(scale, &end); if (!end || *end || !std::isfinite(s.scale) || s.scale <= 0.0f || s.scale > 8.0f) s.scale = 1.0f;
        if (s.resolution && s.fitWindow) Log("Neptasm FitWindow is applied by the next swap-chain creation; WindowWidth/Height remain the fallback");
        if (!ApplyStatic(context, s)) { Log("Neptasm static patch signature mismatch; no static feature was enabled"); return false; }
        if (!(s.resolution || s.ultrawide || s.fitWindow)) { state = candidate.release(); Log("Neptasm static features installed"); return true; }
        state = candidate.release(); state->d3d11 = GetModuleHandleW(L"d3d11.dll");
        if (!state->d3d11) return false;
        originalCreate = reinterpret_cast<CreateDeviceFn>(GetProcAddress(state->d3d11, "D3D11CreateDeviceAndSwapChain"));
        if (!originalCreate || !ReplaceImport(context, 0x4ed4b0, reinterpret_cast<void*>(originalCreate), reinterpret_cast<void*>(CreateDevice))) return false;
        Log("Neptasm graphics installed resolution=%d scale=%.3f ultrawide=%d", s.resolution, s.scale, s.ultrawide); return true;
    } catch (...) { Log("Neptasm installation failed"); return false; }
}
}
