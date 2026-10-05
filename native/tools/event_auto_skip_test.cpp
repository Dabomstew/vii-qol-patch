#include "event_auto_skip.hpp"
#include "patch.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vii {
void Log(const char*, ...) noexcept {}
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks=0, originalCalls=0, callbackCalls=0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); std::exit(1); } } while(false)
alignas(4) static std::array<unsigned char,0xa38> object{};
static void* current=object.data();
template<class T> T& Field(uintptr_t offset) { return *reinterpret_cast<T*>(object.data()+offset); }
static void* __cdecl Active() { return current; }
static int __fastcall Original(void* slot) { CHECK(slot==object.data()+0xa30 || slot==nullptr); ++originalCalls; return 27; }
static int __cdecl Other(unsigned,void*) { CHECK(false); return 0; }
static int __cdecl Skip(unsigned command,void* argument) {
    CHECK(command==0xc && argument==nullptr);
    CHECK(Field<uintptr_t>(0x64)==0 && !(Field<uint32_t>(0x1c)&0x380));
    ++callbackCalls; Field<uint32_t>(0x1c)|=0x80; return 1;
}
static EventSkipAdapter* underTest=nullptr;
static int __fastcall AdapterThunk(void* slot) { return underTest->Poll(slot); }
static void Reset() {
    object.fill(0); current=object.data();
    Field<uintptr_t>(0x22c)=0x1234; Field<EventCallback>(0xa30)=Skip;
}
int main() {
    static_assert(sizeof(void*)==4,"x86 ABI test");
    EventSkipAdapter adapter;
    adapter.original=Original; adapter.active=Active; adapter.supported=Skip;
    underTest=&adapter; Reset();
    auto slot=object.data()+0xa30;
    CHECK(adapter.Poll(slot)==27 && callbackCalls==0); // Disabled/install failure.
    adapter.enabled.store(true);
    for (unsigned flag : {1u,0x80u,0x100u,0x200u}) {
        Reset(); Field<uint32_t>(0x1c)=flag;
        auto before=object;
        CHECK(adapter.Poll(slot)==27 && object==before && callbackCalls==0);
    }
    Reset(); Field<uint32_t>(0x20)=0x10; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); Field<uintptr_t>(0x22c)=0; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); Field<uintptr_t>(0x64)=0x99; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); Field<EventCallback>(0xa30)=Other; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); Field<EventCallback>(0xa30)=nullptr; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); current=nullptr; CHECK(adapter.Poll(slot)==27 && callbackCalls==0);
    Reset(); CHECK(adapter.Poll(nullptr)==27 && callbackCalls==0);
    Reset();
    // Execute an actual x86 ECX-only caller with an E8 replacement and test ESP.
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(code!=nullptr);
    const unsigned char thunk[]={0x8b,0x4c,0x24,0x04,0xe8,0,0,0,0,0xc3};
    std::memcpy(code,thunk,sizeof(thunk));
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(AdapterThunk)-reinterpret_cast<uintptr_t>(code)-9);
    std::memcpy(code+5,&relative,4);
    DWORD old=0; CHECK(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),code,4096));
    auto caller=reinterpret_cast<int(__cdecl*)(void*)>(code);
    unsigned beforeStack=0,afterStack=0;
    __asm mov beforeStack,esp
    const int result=caller(slot);
    __asm mov afterStack,esp
    CHECK(result==1 && callbackCalls==1 && beforeStack==afterStack);
    CHECK(Field<uint32_t>(0x1c)==0x80 && Field<uintptr_t>(0x64)==0);
    // Engine finish latch prevents another automatic command in this state.
    CHECK(caller(slot)==27 && callbackCalls==1);
    CHECK(VirtualFree(code,0,MEM_RELEASE));
    std::printf("PASS: %u checks; disabled/unknown/blocked/modal guards, one engine command, finish latch, x86 ECX and ESP; original calls=%u\n",checks,originalCalls);
    return 0;
}
