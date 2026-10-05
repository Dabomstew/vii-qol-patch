#include "patch.hpp"
#include "battle_auto_skip.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vii {
void Log(const char*, ...) noexcept {}
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
static BattleSkipAdapter adapter;
static int __fastcall Thunk(void* p, void*, unsigned mask) { return adapter.Poll(p,mask); }
int main() {
    static_assert(sizeof(void*) == 4, "x86 ABI");
    auto code = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(code);
    const unsigned char original[] = {0x55,0x8b,0xec,0x8b,0x41,0x18,0x23,0x45,0x08,0x5d,0xc2,0x04,0};
    std::memcpy(code,original,sizeof(original));
    std::memcpy(code+128,original,sizeof(original));
    code[128+5]=0x1c; // Edge-input field used by the result gate.
    // cdecl wrapper receives (object,mask), sets ECX, pushes only mask, calls
    // the actual fastcall adapter and relies on its RET4 to clean the stack.
    const unsigned char caller[] = {0x8b,0x4c,0x24,0x04,0xff,0x74,0x24,0x08,0xe8,0,0,0,0,0xc3};
    std::memcpy(code+32,caller,sizeof(caller));
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Thunk)-reinterpret_cast<uintptr_t>(code+32)-13);
    std::memcpy(code+32+9,&relative,4);
    DWORD old=0; CHECK(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),code,4096));
    const auto invoke=reinterpret_cast<int(__cdecl*)(void*,unsigned)>(code+32);
    std::array<unsigned,16> input{};
    // Physical controller remains byte-for-byte untouched for all combinations.
    for (const unsigned field : {6u,7u}) {
    adapter.original=reinterpret_cast<BattleInputPoll>(code+(field==6?0:128));
    for (const unsigned bits : {0u,1u,2u,3u,0xffffffffu}) {
        input.fill(~bits); input[field]=bits;
        const auto before=input;
        for (const unsigned mask : {1u,2u,3u,0x40u,0x800000u}) {
            adapter.enabled.store(false);
            CHECK(static_cast<unsigned>(invoke(input.data(),mask))==(bits&mask));
            adapter.enabled.store(true);
            unsigned beforeStack=0,afterStack=0;
            __asm mov beforeStack,esp
            const auto result=static_cast<unsigned>(invoke(input.data(),mask));
            __asm mov afterStack,esp
            CHECK(result==(mask==1?1u:(bits&mask)));
            CHECK(beforeStack==afterStack && input==before);
        }
    }
    }
    CHECK(adapter.forced.load()==10);
    CHECK(VirtualFree(code,0,MEM_RELEASE));
    std::printf("PASS %u checks: original mask reads, disabled pass-through, LT-only replacement, input immutability, ECX/stack ABI\n",checks);
}
