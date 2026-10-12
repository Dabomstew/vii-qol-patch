#include "early_graphics.hpp"
#include <d3d11.h>
#include <cstring>

namespace vii {
namespace {
using CreateFn = decltype(&D3D11CreateDeviceAndSwapChain);
void** creationSlot = nullptr;
CreateFn originalCreation = nullptr;
void (*initializeGraphics)() noexcept = nullptr;

bool Exchange(void** slot, void* expected, void* replacement) noexcept {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    auto previous = InterlockedCompareExchangePointer(slot, replacement, expected);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    return previous == expected;
}

// Parse names instead of trusting a game RVA before the one startup hash.
// No allocations, file I/O, library loading or patch initialization in DllMain.
void** FindCreationSlot(HMODULE game) noexcept {
    __try {
        const auto base = reinterpret_cast<unsigned char*>(game);
        if (!base) return nullptr;
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return nullptr;
        const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return nullptr;
        const size_t size = nt->OptionalHeader.SizeOfImage;
        const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!imports.VirtualAddress || imports.VirtualAddress >= size || imports.Size > size - imports.VirtualAddress) return nullptr;
        for (size_t at = 0; at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size; at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            const auto entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress + at);
            if (!entry->Name) break;
            if (entry->Name >= size || size - entry->Name < sizeof("d3d11.dll") ||
                _strnicmp(reinterpret_cast<char*>(base + entry->Name), "d3d11.dll", sizeof("d3d11.dll"))) continue;
            const size_t lookup = entry->OriginalFirstThunk;
            const size_t first = entry->FirstThunk;
            if (!lookup || !first || lookup >= size || first >= size) return nullptr;
            for (size_t offset = 0; offset + 4 <= size - lookup && offset + 4 <= size - first; offset += 4) {
                const auto name = *reinterpret_cast<DWORD*>(base + lookup + offset);
                if (!name) break;
                if (IMAGE_SNAP_BY_ORDINAL32(name)) continue;
                if (name >= size || size - name < 2 + sizeof("D3D11CreateDeviceAndSwapChain")) return nullptr;
                if (!std::memcmp(base + name + 2, "D3D11CreateDeviceAndSwapChain", sizeof("D3D11CreateDeviceAndSwapChain")))
                    return reinterpret_cast<void**>(base + first + offset);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

HRESULT WINAPI Bootstrap(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk, const DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** chain, ID3D11Device** device, D3D_FEATURE_LEVEL* feature, ID3D11DeviceContext** context) {
    // Restore before initialization so the ordinary guarded Neptasm installer
    // sees the original import and can replace it with its own graphics hook.
    const bool restored = Exchange(creationSlot, reinterpret_cast<void*>(Bootstrap), reinterpret_cast<void*>(originalCreation));
    if (restored && initializeGraphics) initializeGraphics();
    const auto current = restored ? reinterpret_cast<CreateFn>(*creationSlot) : originalCreation;
    const auto forward = current && current != Bootstrap ? current : originalCreation;
    return forward(adapter, type, software, flags, levels, levelCount, sdk, desc, chain, device, feature, context);
}
}

bool InstallEarlyGraphicsBootstrap(HMODULE game, void (*initialize)() noexcept) noexcept {
    const auto slot = FindCreationSlot(game);
    const auto d3d = GetModuleHandleW(L"d3d11.dll");
    const auto original = d3d ? GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain") : nullptr;
    if (!slot || !original || *slot != reinterpret_cast<void*>(original)) return false;
    creationSlot = slot; originalCreation = reinterpret_cast<CreateFn>(original); initializeGraphics = initialize;
    return Exchange(slot, reinterpret_cast<void*>(original), reinterpret_cast<void*>(Bootstrap));
}
}
