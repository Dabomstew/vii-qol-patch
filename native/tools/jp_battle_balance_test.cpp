#include "jp_battle_balance.hpp"
#include "code_calls.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <xmmintrin.h>
namespace vii { void Log(const char*,...) noexcept {} }
using namespace vii::jp_balance;
static unsigned checks=0;
#define CHECK(x) do { ++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(false)
template<class T> static void Put(unsigned char* p,T v) { std::memcpy(p,&v,sizeof(v)); }
template<class T> static T Get(const unsigned char* p) { T v;std::memcpy(&v,p,sizeof(v));return v; }
static float Oracle(float p,float s,float t,uint8_t c) {
    volatile float half=t*0.5f,term=s+half,scaled=p*((c&15)?1.9f:8.0f);
    volatile float divided=scaled/1000.0f,result=divided*term;return result;
}
static float Additive(float p,float s,float t) { volatile float a=p+s,b=t*0.5f;return a+b; }
alignas(16) static unsigned char beforeFX[512]{},afterFX[512]{};
static void* target=nullptr;static unsigned char* frame=nullptr;
static uint32_t registers[7]{},flagsAfter=0,stackBefore=0,stackAfter=0;
__declspec(naked) static void InvokeDamage() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp,frame
        fxrstor beforeFX
        mov eax,11111111h
        mov ebx,22222222h
        mov ecx,33333333h
        mov edx,44444444h
        mov esi,55555555h
        mov edi,66666666h
        push 246h
        popfd
        mov stackBefore,esp
        call target
        mov stackAfter,esp
        pushfd
        pop flagsAfter
        mov registers[0],eax
        mov registers[4],ebx
        mov registers[8],ecx
        mov registers[12],edx
        mov registers[16],esi
        mov registers[20],edi
        mov registers[24],ebp
        fxsave afterFX
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static unsigned char* Mapping(std::array<unsigned char*,4>& sites) {
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,16384,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(p);
    for(size_t i=0;i<4;++i) {
        sites[i]=p+i*4096+32;
        std::memcpy(sites[i],Sites[i].signature.data(),8);
        if(i<2) {
            // Synthetic cdecl baseline: original threshold 123, return 7.
            const unsigned char rest[]={0x00,0x00,0x00,0x8b,0x45,0x0c,0xc7,0x00,123,0,0,0,0xb8,7,0,0,0,0x5d,0xc3};
            std::memcpy(sites[i]+8,rest,sizeof(rest));
        } else sites[i][8]=0xc3;
    }
    DWORD old=0;CHECK(VirtualProtect(p,16384,PAGE_EXECUTE_READ,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),p,16384));return p;
}
static void CheckDamage(unsigned char* address,bool part,bool enabled,uint32_t flags,uint16_t level,uint8_t category,
                        float power,float magic,float str,float intelligence,float tec) {
    alignas(16) unsigned char storage[1024]{},record[64]{},action[64]{},stats[32]{};
    frame=storage+640;target=address;SetTestGate(enabled);
    Put(record,flags);Put(record+0x1a,level);action[0x20]=category;
    Put(stats,str);Put(stats+8,intelligence);Put(stats+16,tec);
    if(part) {
        Put(frame+0xc,record);Put(frame+0x10,stats);Put(frame+0x1c,action);
        Put(frame-0x120,power);Put(frame-0x148,magic);Put(frame-0x130,1.2f);
    } else {
        Put(frame-0x238,record);Put(frame+0x14,action);Put(frame-0x258,power);Put(frame-0x25c,magic);
        Put(frame-0x214,str);Put(frame-0x20c,intelligence);Put(frame-0x204,tec);Put(frame-0x22c,1.2f);
    }
    __asm fxsave beforeFX
    Put(beforeFX+24,uint32_t(0x1f80));
    for(unsigned i=0;i<32;++i)Put(beforeFX+160+4*i,float(i+10));
    const unsigned magical=part?0:2;
    Put(beforeFX+160+3*16,Additive(power,str,tec));Put(beforeFX+160+magical*16,Additive(magic,intelligence,tec));
    InvokeDamage();
    CHECK(stackBefore==stackAfter);CHECK((flagsAfter&0xcd5)==(0x246&0xcd5));
    for(unsigned i=0;i<6;++i)CHECK(registers[i]==0x11111111*(i+1));
    CHECK(registers[6]==reinterpret_cast<uintptr_t>(frame));
    const bool high=enabled&&level>100-((flags>>2)&1);
    const float physical=(high?Oracle(power,str,tec,category):Additive(power,str,tec))*1.2f;
    const float elemental=high?Oracle(magic,intelligence,tec,category):Additive(magic,intelligence,tec);
    CHECK(Get<float>(afterFX+160+3*16)==physical);CHECK(Get<float>(afterFX+160+magical*16)==elemental);
    CHECK((Get<uint32_t>(afterFX+24)&~uint32_t(63))==Get<uint32_t>(beforeFX+24));
    CHECK(!std::memcmp(beforeFX,afterFX,24));CHECK(!std::memcmp(beforeFX+32,afterFX+32,128));
    for(unsigned i=0;i<32;++i)if(i!=12&&i!=magical*4)CHECK(Get<uint32_t>(beforeFX+160+4*i)==Get<uint32_t>(afterFX+160+4*i));
}
int main() {
    static_assert(sizeof(void*)==4,"real x86 adapters required");
    const Tier hits[]={{20,5},{50,10},{80,50},{100,75},{200,100},{300,200},{500,300},{800,500},{1000,999}};
    const Tier credits[]={{1000,1},{3000,3},{5000,5},{8000,8},{10000,10},{20000,15},{50000,30},{100000,50},
        {200000,100},{300000,200},{400000,300},{500000,400},{600000,500},{700000,600},{800000,700},{900000,800}};
    for(bool credit:{false,true}) {
        auto fn=credit?CreditBonus:HitBonus;const auto* tiers=credit?credits:hits;const unsigned n=credit?16:9;
        uint32_t out=999;CHECK(fn(0,&out)==0&&out==0);
        for(unsigned i=0;i<n;++i)for(int delta=-1;delta<=1;++delta) {
            const uint32_t count=tiers[i].threshold+delta;
            const auto expected=delta<0?(i?tiers[i-1]:Tier{0,0}):tiers[i];
            CHECK(fn(count,&out)==expected.percent&&out==expected.threshold);
        }
        CHECK(fn(UINT32_MAX,&out)==tiers[n-1].percent&&out==tiers[n-1].threshold);
    }
    for(uint32_t flags:{0u,4u})for(uint16_t level:{uint16_t(99),uint16_t(100),uint16_t(101)})
        CHECK(HighLevel(flags,level)==(flags?level>=100:level>=101));
    std::array<unsigned char*,4> sites{};
    // Rejection before any writes, actual transaction rollback, and every partial-install boundary.
    for(int fail=-2;fail<4;++fail) {
        auto* mapping=Mapping(sites);
        auto original=Sites[0].signature;
        if(fail==-2) { DWORD old=0;CHECK(VirtualProtect(mapping,16384,PAGE_READWRITE,&old));sites[3][0]^=1;CHECK(VirtualProtect(mapping,16384,PAGE_EXECUTE_READ,&old)); }
        if(fail==-1)vii::SetCallFault(vii::CallFault::AfterFirstWrite);
        CHECK(!InstallTestSites(sites,fail>=0?fail:-1));CHECK(!TestActive());
        if(fail<1)CHECK(!std::memcmp(sites[0],original.data(),8));
        for(unsigned i=0;i<2;++i) { uint32_t out=0;auto fn=reinterpret_cast<uint32_t(__cdecl*)(uint32_t,uint32_t*)>(sites[i]);CHECK(fn(1000,&out)==7&&out==123); }
        // A failed installation must not publish any high-level behavior.
        if(fail==3)CheckDamage(sites[2],false,false,4,100,1,3600,3300,1000,500,120);
        CHECK(VirtualFree(mapping,0,MEM_RELEASE));ResetTestState();
    }
    auto* mapping=Mapping(sites);CHECK(InstallTestSites(sites));CHECK(TestActive());
    uint32_t threshold=0;CHECK(reinterpret_cast<uint32_t(__cdecl*)(uint32_t,uint32_t*)>(sites[0])(80,&threshold)==50&&threshold==80);
    CHECK(reinterpret_cast<uint32_t(__cdecl*)(uint32_t,uint32_t*)>(sites[1])(900000,&threshold)==800&&threshold==900000);
    for(bool part:{false,true})for(bool enabled:{false,true})for(uint32_t flags:{0u,4u})
        for(uint16_t level:{uint16_t(99),uint16_t(100),uint16_t(101)})for(uint8_t category:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3),uint8_t(0x10)})
            for(auto powers:{std::pair<float,float>{3600.f,0.f},{0.f,3300.f},{3820.f,3820.f},{1500.f,3300.f},{0.f,0.f}})
                CheckDamage(sites[part?3:2],part,enabled,flags,level,category,powers.first,powers.second,1000,750,123);
    // The adapters consume already selected primary/follow-up powers and
    // effective stats, including fractional cooperative means. Poison raw
    // action power bytes (left zero by CheckDamage) must not influence them.
    for(bool part:{false,true})for(bool enabled:{false,true})for(uint32_t flags:{0u,4u})
        for(uint16_t level:{uint16_t(99),uint16_t(100),uint16_t(101)})
            for(uint8_t category:{uint8_t(0),uint8_t(1)}) {
                CheckDamage(sites[part?3:2],part,enabled,flags,level,category,900.25f,825.75f,101.5f,88.25f,124.75f);
                CheckDamage(sites[part?3:2],part,enabled,flags,level,category,0.f,3300.f,0.f,0.f,0.f);
                CheckDamage(sites[part?3:2],part,enabled,flags,level,category,1500.5f,3300.5f,16777216.f,999.875f,3.f);
            }
    CHECK(VirtualFree(mapping,0,MEM_RELEASE));ResetTestState();
    std::printf("PASS: %u checks; tiers, cutoffs, real adapters, GPR/EFLAGS/SSE/x87/control state, continuations, failures\n",checks);
}
