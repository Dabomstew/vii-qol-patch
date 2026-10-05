#include "patch.hpp"
#include "tutorial_suppression.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vii {
void Log(const char*, ...) noexcept {}
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks = 0, calls = 0, lastId = 0, result = 0;
static TutorialSuppressionAdapter* testing = nullptr;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
static uint32_t __cdecl Original(uint32_t id) { ++calls; lastId = id; return result; }
static uint32_t __cdecl Query(uint32_t id) { return testing->Query(id, "event"); }
int main() {
    static_assert(sizeof(void*) == 4, "x86 only");
    TutorialSuppressionAdapter a; a.original = Original; testing = &a;
    CHECK(a.Query(3030, "event") == 0 && calls == 1 && lastId == 3030);
    a.enabled.store(true);
    for (auto kind : {"event", "catalog"}) {
        for (auto id : {1u, 3030u, 0xffffu}) {
            result = 0; CHECK(a.Query(id, kind) == 1 && lastId == id);
            result = 1; CHECK(a.Query(id, kind) == 1);
            result = 0x76543200; CHECK(a.Query(id, kind) == 0x76543201);
        }
    }
    result = 0;
    for (auto id : {0u, 0x10000u, 0xffffffffu}) CHECK(a.Query(id, "event") == 0);
    CHECK(Original(3030) == 0); // Shared/manual query remains unchanged.
    const unsigned char body[] = {0xff,0x74,0x24,0x04,0xe8,0,0,0,0,0x83,0xc4,0x04,0xc3};
    auto code = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(code != nullptr); std::memcpy(code,body,sizeof(body));
    const auto rel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Query) - reinterpret_cast<uintptr_t>(code + 9));
    std::memcpy(code + 5,&rel,4);
    DWORD old=0; CHECK(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),code,sizeof(body)));
    auto invoke = reinterpret_cast<HelpSeenCheck>(code);
    unsigned before=0,after=0;
    __asm mov before,esp
    const auto answer=invoke(3030);
    __asm mov after,esp
    CHECK(answer==1 && lastId==3030 && before==after);
    a.enabled.store(false); CHECK(invoke(3030)==0);
    CHECK(VirtualFree(code,0,MEM_RELEASE));
    std::printf("PASS: %u checks; automatic-only result substitution, disabled/invalid guards, shared query unchanged, cdecl argument and ESP\n",checks);
    return 0;
}
