#include "jp_battle_balance.hpp"
#include "code_calls.hpp"
#include <atomic>
#include <cstring>
#include <xmmintrin.h>

namespace vii::jp_balance {
namespace {
constexpr Tier Hits[] = {{20,5},{50,10},{80,50},{100,75},{200,100},
                         {300,200},{500,300},{800,500},{1000,999}};
constexpr Tier Credits[] = {{1000,1},{3000,3},{5000,5},{8000,8},{10000,10},
    {20000,15},{50000,30},{100000,50},{200000,100},{300000,200},{400000,300},
    {500000,400},{600000,500},{700000,600},{800000,700},{900000,800}};
template<size_t N> uint32_t Bonus(uint32_t count, uint32_t* threshold, const Tier (&tiers)[N]) noexcept {
    *threshold = 0;
    for (size_t i=N; i; --i) if (count >= tiers[i-1].threshold) {
        *threshold = tiers[i-1].threshold; return tiers[i-1].percent;
    }
    return 0;
}
std::atomic<bool> active{false};
bool attempted = false;
using Resolver = uint32_t (__cdecl*)(uint32_t, uint32_t*);
void* trampolines[4]{};
void* ordinaryTrampoline = nullptr;
void* partTrampoline = nullptr;
template<class T> T Read(const unsigned char* address) noexcept {
    T value; std::memcpy(&value,address,sizeof(value)); return value;
}
// FXSAVE keeps all original lanes, x87 state and MXCSR; only two low lanes change.
void __cdecl Ordinary(unsigned char* frame, unsigned char* saved) noexcept {
    if (!active.load(std::memory_order_acquire)) return;
    auto* record = Read<unsigned char*>(frame-0x238);
    if (!record || !HighLevel(Read<uint32_t>(record),Read<uint16_t>(record+0x1a))) return;
    auto* action = Read<unsigned char*>(frame+0x14);
    const uint8_t category = action ? action[0x20] : 1;
    const float tec = Read<float>(frame-0x204);
    const float physical = AttackComponent(Read<float>(frame-0x258),Read<float>(frame-0x214),tec,category);
    const float magical = AttackComponent(Read<float>(frame-0x25c),Read<float>(frame-0x20c),tec,category);
    std::memcpy(saved+160+3*16,&physical,4);
    std::memcpy(saved+160+2*16,&magical,4);
}
void __cdecl Part(unsigned char* frame, unsigned char* saved) noexcept {
    if (!active.load(std::memory_order_acquire)) return;
    auto* record = Read<unsigned char*>(frame+0x0c);
    if (!record || !HighLevel(Read<uint32_t>(record),Read<uint16_t>(record+0x1a))) return;
    auto* action = Read<unsigned char*>(frame+0x1c);
    auto* stats = Read<unsigned char*>(frame+0x10);
    const uint8_t category = action ? action[0x20] : 1;
    const float tec = Read<float>(stats+16);
    const float physical = AttackComponent(Read<float>(frame-0x120),Read<float>(stats),tec,category);
    const float magical = AttackComponent(Read<float>(frame-0x148),Read<float>(stats+8),tec,category);
    std::memcpy(saved+160+3*16,&physical,4);
    std::memcpy(saved+160,&magical,4);
}
__declspec(naked) void OrdinaryAdapter() {
    __asm {
        pushfd
        pushad
        sub esp,544
        lea eax,[esp+15]
        and eax,-16
        fxsave [eax]
        push eax
        push ebp
        call Ordinary
        add esp,8
        lea eax,[esp+15]
        and eax,-16
        fxrstor [eax]
        add esp,544
        popad
        popfd
        jmp dword ptr [ordinaryTrampoline]
    }
}
__declspec(naked) void PartAdapter() {
    __asm {
        pushfd
        pushad
        sub esp,544
        lea eax,[esp+15]
        and eax,-16
        fxsave [eax]
        push eax
        push ebp
        call Part
        add esp,8
        lea eax,[esp+15]
        and eax,-16
        fxrstor [eax]
        add esp,544
        popad
        popfd
        jmp dword ptr [partTrampoline]
    }
}
uint32_t __cdecl HitAdapter(uint32_t count,uint32_t* threshold) noexcept {
    if (active.load(std::memory_order_acquire)) return HitBonus(count,threshold);
    return reinterpret_cast<Resolver>(trampolines[0])(count,threshold);
}
uint32_t __cdecl CreditAdapter(uint32_t count,uint32_t* threshold) noexcept {
    if (active.load(std::memory_order_acquire)) return CreditBonus(count,threshold);
    return reinterpret_cast<Resolver>(trampolines[1])(count,threshold);
}
void* const adapters[] = {reinterpret_cast<void*>(HitAdapter),reinterpret_cast<void*>(CreditAdapter),
                         reinterpret_cast<void*>(OrdinaryAdapter),reinterpret_cast<void*>(PartAdapter)};
bool Install(const std::array<unsigned char*,4>& addresses,int failBefore) {
    if (attempted) return active.load(std::memory_order_acquire);
    attempted = true;
    for (size_t i=0;i<Sites.size();++i) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!addresses[i] || VirtualQuery(addresses[i],&memory,sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || memory.Protect != PAGE_EXECUTE_READ ||
            reinterpret_cast<uintptr_t>(addresses[i])+8 > reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize ||
            std::memcmp(addresses[i],Sites[i].signature.data(),8)) {
            Log("JPBattleBalance signature/protection unavailable at site %u",unsigned(i)); return false;
        }
    }
    // No game bytes are written until every state pointer and RX trampoline exists.
    for (size_t i=0;i<Sites.size();++i) {
        auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if (!code) return false;
        trampolines[i]=code;
        const auto span=Sites[i].displaced;
        std::memcpy(code,addresses[i],span);code[span]=0xe9;
        const auto relative=uint32_t(reinterpret_cast<uintptr_t>(addresses[i]+span)-reinterpret_cast<uintptr_t>(code+span+5));
        std::memcpy(code+span+1,&relative,4);
        DWORD old=0;
        if (!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,span+5)) return false;
    }
    ordinaryTrampoline=trampolines[2];partTrampoline=trampolines[3];
    for (size_t i=0;i<Sites.size();++i) {
        if (int(i)==failBefore) return false;
        const auto report=ReplaceEntryJump({addresses[i],Sites[i].signature,adapters[i]});
        Log("JPBattleBalance site=%u hook=%s installed=%d rollback=%d resume_failures=%u",
            unsigned(i),CallStatusName(report.status),report.installed,report.rolledBack,report.resumeFailures);
        if (!report.installed || report.status!=CallStatus::Installed || !report.protectionRestored || report.resumeFailures) return false;
    }
    active.store(true,std::memory_order_release);return true;
}
}
uint32_t HitBonus(uint32_t count,uint32_t* threshold) noexcept { return Bonus(count,threshold,Hits); }
uint32_t CreditBonus(uint32_t count,uint32_t* threshold) noexcept { return Bonus(count,threshold,Credits); }
bool HighLevel(uint32_t flags,uint16_t level) noexcept { return level > 100-((flags>>2)&1); }
float AttackComponent(float power,float stat,float tec,uint8_t category) noexcept {
    const auto term=_mm_add_ss(_mm_set_ss(stat),_mm_mul_ss(_mm_set_ss(tec),_mm_set_ss(0.5f)));
    const auto scaled=_mm_div_ss(_mm_mul_ss(_mm_set_ss(power),_mm_set_ss((category&15)?1.9f:8.0f)),_mm_set_ss(1000.0f));
    return _mm_cvtss_f32(_mm_mul_ss(scaled,term));
}
#ifdef VII_JP_BALANCE_TESTING
bool InstallTestSites(const std::array<unsigned char*,4>& addresses,int failBefore) { return Install(addresses,failBefore); }
void ResetTestState() {
    active.store(false);attempted=false;
    for (auto& code:trampolines) { if(code)VirtualFree(code,0,MEM_RELEASE);code=nullptr; }
    ordinaryTrampoline=partTrampoline=nullptr;
}
bool TestActive() { return active.load(); }
void* TestAdapter(size_t index) { return adapters[index]; }
void SetTestGate(bool enabled) { active.store(enabled); }
#endif
}
namespace vii {
bool InstallJPBattleBalance(const Context& context) {
    std::array<unsigned char*,4> addresses{};
    for (size_t i=0;i<addresses.size();++i)
        addresses[i]=reinterpret_cast<unsigned char*>(context.game)+jp_balance::Sites[i].rva;
    return jp_balance::Install(addresses,-1);
}
}
