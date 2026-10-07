#include "dungeon_preview.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace vii {
void Log(const char*,...) noexcept {}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks=0,calls=0;
static void* lastData=nullptr;static void* lastMedia=nullptr;
#define CHECK(x) do { ++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(false)
static void __fastcall Original(void* data,void*,void* media) { ++calls;lastData=data;lastMedia=media; }
static DungeonPreviewAdapter* active=nullptr;
static void __fastcall Invoke(void* data,void*,void* media) { active->Prepare(data,media); }
int main() {
    static_assert(sizeof(void*)==4,"x86 only");
    DungeonPreviewAdapter a;active=&a;a.original=reinterpret_cast<PreviewPrepare>(Original);
    uint32_t data[21],before[21];
    for(unsigned i=0;i<21;++i)data[i]=0x12340000+i;
    data[3]=data[5]=0;std::memcpy(before,data,sizeof(data));
    auto* media=reinterpret_cast<void*>(0xabcdef00);
    a.Prepare(data,media);CHECK(calls==1&&lastData==data&&lastMedia==media);
    CHECK(!std::memcmp(data,before,sizeof(data)));
    a.enabled.store(true);a.Prepare(data,media);CHECK(calls==1);
    CHECK(data[10]==2&&data[11]==0&&data[12]==0);
    for(unsigned i=0;i<21;++i)if(i<10||i>12)CHECK(data[i]==before[i]);
    a.Prepare(data,media);CHECK(calls==1); // Repeated paint has no allocation or loading.
    data[3]=0x12345678;a.Prepare(data,media);CHECK(calls==2&&data[3]==0x12345678);
    data[3]=0;data[5]=0x23456789;a.Prepare(data,media);CHECK(calls==3&&data[5]==0x23456789);
    a.Prepare(nullptr,media);CHECK(calls==4&&lastData==nullptr&&lastMedia==media);
    data[5]=0;
    auto invoke=reinterpret_cast<PreviewPrepare>(Invoke);unsigned stackBefore=0,stackAfter=0;
    __asm mov stackBefore,esp
    invoke(data,media);
    __asm mov stackAfter,esp
    CHECK(stackBefore==stackAfter&&calls==4);
    a.enabled.store(false);invoke(data,media);CHECK(calls==5&&lastData==data&&lastMedia==media);
    std::printf("PASS: %u checks; empty media only, existing handles/default-off passthrough, fields preserved, thiscall RET4/ESP\n",checks);
    return 0;
}
