#include "ordered_event_keys.hpp"
#include "event_skip_buffer.hpp"
#include "modal_key_queue.hpp"
#include "code_calls.hpp"
#include <mutex>
#include <cstring>

namespace vii {
namespace {
std::mutex mutex;
ModalKeyQueue queue;
std::atomic<bool> enabled{false};
HANDLE ready=nullptr;
HANDLE stop=nullptr;
HANDLE wake=nullptr;
std::atomic<bool> captureInstalled{false};
DWORD process=0;
unsigned keys[3]={VK_UP,VK_DOWN,VK_RETURN};
uintptr_t base=0;
void* owner=nullptr; void* handle=nullptr; void* setupOwner=nullptr;
unsigned scene=0;
unsigned observed=0,matched=0,foregroundEvents=0;
thread_local bool scope=false;
using ModalUpdate=unsigned char(__cdecl*)(void*,void*);
ModalUpdate modalUpdate=nullptr;
EventInputEdge edge=nullptr,repeatEdge=nullptr;
ActiveEvent active=nullptr;
template<class T> T Field(void* p,unsigned offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset); }
bool Foreground() { DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid==process; }
void Close() {
    unsigned captures=0,replays=0,id=0,seen=0,matches=0,focused=0;
    { std::lock_guard<std::mutex> lock(mutex);
        if(owner){id=scene;captures=queue.Captures();replays=queue.Replays();}
        seen=observed;matches=matched;focused=foregroundEvents;
        queue.Close(); owner=handle=setupOwner=nullptr; scene=0;
    }
    if(id)Log("EventSkipBuffer ordered modal scene=%u captures=%u replays=%u seen=%u matched=%u foreground=%u",id,captures,replays,seen,matches,focused);
}
DWORD WINAPI Listen(void*) {
    // No global keyboard hook or engine pointers on this thread. The high bit
    // observes held state; the unreliable GetAsyncKeyState low bit is unused.
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
    const bool installed=timer!=nullptr;
    captureInstalled.store(installed,std::memory_order_release);
    SetEvent(ready);
    if(!installed){if(timer)CloseHandle(timer);return 1;}
    HANDLE idle[]={stop,wake},waits[]={stop,timer};
    bool running=true;
    while(running&&WaitForMultipleObjects(2,idle,FALSE,INFINITE)==WAIT_OBJECT_0+1) {
        LARGE_INTEGER due{};due.QuadPart=-10000;
        if(!SetWaitableTimer(timer,&due,1,nullptr,nullptr,FALSE))break;
        for(;;) {
            if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0+1){running=false;break;}
            // Serializing the snapshot with Open/Close prevents stale epochs.
            std::lock_guard<std::mutex> lock(mutex);
            if(!queue.IsOpen())break;
            if(!Foreground()){queue.Close();break;}
            ++observed;++foregroundEvents;
            unsigned held=0;
            for(unsigned i=0;i<3;++i){const bool down=(GetAsyncKeyState(keys[i])&0x8000)!=0;
                if(down){++matched;held|=1u<<i;}}
            queue.Sample(held,GetTickCount64());
        }
        CancelWaitableTimer(timer);
    }
    CancelWaitableTimer(timer);CloseHandle(timer);
    captureInstalled.store(false,std::memory_order_release);return 0;
}
bool Valid(void* setup,void* controller) {
    return setup&&controller&&controller==active()&&
        Field<EventCallback>(controller,0xa30)==reinterpret_cast<EventCallback>(base+0x91600)&&
        Field<unsigned char>(setup,0x375c)==7&&Field<unsigned>(controller,0x22c)&&
        !(Field<unsigned>(controller,0x20)&0x10)&&
        (Field<unsigned>(controller,0x1c)&0x200)&&Field<void*>(controller,0x64)&&Foreground();
}
unsigned char __cdecl Modal(void* setup,void* controller) {
    if (!enabled.load(std::memory_order_acquire)) return modalUpdate(setup,controller);
    if (!Valid(setup,controller)) { Close(); return modalUpdate(setup,controller); }
    if (!captureInstalled.load(std::memory_order_acquire)) { Close(); return modalUpdate(setup,controller); }
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto current=Field<void*>(controller,0x64); const auto id=Field<unsigned>(setup,0x10);
        if (!queue.IsOpen()||owner!=controller||handle!=current||setupOwner!=setup||scene!=id) {
            unsigned held=0;
            for(unsigned i=0;i<3;++i)if(GetAsyncKeyState(keys[i])&0x8000)held|=1u<<i;
            queue.Open(held);owner=controller;handle=current;setupOwner=setup;scene=id;
            SetEvent(wake);
        }
        queue.Tick();
    }
    const bool priorScope=scope; scope=true;
    const auto result=modalUpdate(setup,controller);
    scope=priorScope;
    if (!Valid(setup,controller)) Close();
    return result;
}
int Query(void* device,unsigned mask,unsigned key,bool repeat) {
    const int physical=(repeat?repeatEdge:edge)(device,mask);
    if (!scope||!enabled.load(std::memory_order_acquire)) return physical;
    std::lock_guard<std::mutex> lock(mutex);
    if (!Foreground()) { queue.Close(); return physical; }
    const auto polled=Field<unsigned>(device,0x18)|Field<unsigned>(device,0x1c)|Field<unsigned>(device,0x20);
    return queue.Query(key,physical,GetTickCount64(),repeat,(polled&mask)!=0)?
        (physical?physical:static_cast<int>(mask)):0;
}
int __fastcall Up(void* d,void*,unsigned m) { return m==0x100000?Query(d,m,0,false):edge(d,m); }
int __fastcall Down(void* d,void*,unsigned m) { return m==0x400000?Query(d,m,1,false):edge(d,m); }
int __fastcall Enter(void* d,void*,unsigned m) { return m==0x40?Query(d,m,2,false):edge(d,m); }
int __fastcall UpRepeat(void* d,void*,unsigned m) { return m==0x100000?Query(d,m,0,true):repeatEdge(d,m); }
int __fastcall DownRepeat(void* d,void*,unsigned m) { return m==0x400000?Query(d,m,1,true):repeatEdge(d,m); }
}
void ResetOrderedEventKeys() { if (enabled.load(std::memory_order_acquire)) Close(); }
bool InstallOrderedEventKeys(const Context& context) {
    if (!Option(context,L"EventSkipBuffer",L"OrderedKeyboard",1)) return true;
    base=reinterpret_cast<uintptr_t>(context.game); process=GetCurrentProcessId();
    const wchar_t* names[]={L"UpKey",L"DownKey",L"ConfirmKey"};
    for (unsigned i=0;i<3;++i) {
        const int value=Option(context,L"EventSkipBuffer",names[i],keys[i]);
        if (value<1||value>254) return false;
        keys[i]=static_cast<unsigned>(value);
        for (unsigned j=0;j<i;++j) if(keys[j]==keys[i])return false;
    }
    modalUpdate=reinterpret_cast<ModalUpdate>(base+0x90cf0);
    active=reinterpret_cast<ActiveEvent>(base+0x331cd0);
    edge=reinterpret_cast<EventInputEdge>(base+0x3a2060);
    repeatEdge=reinterpret_cast<EventInputEdge>(base+0x3a2090);
    const CallSite sites[]={
        {reinterpret_cast<unsigned char*>(base+0x909d5),{0xe8,0x16,0x03,0x00,0x00,0x83,0xc4,0x08},reinterpret_cast<void*>(Modal)},
        {reinterpret_cast<unsigned char*>(base+0x2227b6),{0xe8,0xa5,0xf8,0x17,0x00,0x85,0xc0,0x74},reinterpret_cast<void*>(Up)},
        {reinterpret_cast<unsigned char*>(base+0x2227ee),{0xe8,0x6d,0xf8,0x17,0x00,0x85,0xc0,0x74},reinterpret_cast<void*>(Down)},
        {reinterpret_cast<unsigned char*>(base+0x222819),{0xe8,0x72,0xf8,0x17,0x00,0x85,0xc0,0x74},reinterpret_cast<void*>(UpRepeat)},
        {reinterpret_cast<unsigned char*>(base+0x222843),{0xe8,0x48,0xf8,0x17,0x00,0x85,0xc0,0x74},reinterpret_cast<void*>(DownRepeat)},
        {reinterpret_cast<unsigned char*>(base+0x222906),{0xe8,0x55,0xf7,0x17,0x00,0x85,0xc0,0x75},reinterpret_cast<void*>(Enter)}
    };
    for(const auto& site:sites) if(std::memcmp(site.address,site.expected.data(),8)) return false;
    // Publish only when every guarded adapter is installed. Partial installs pass through.
    for(const auto& site:sites) {
        auto report=ReplaceCall(site);
        if(!report.installed||report.status!=CallStatus::Installed||!report.protectionRestored||report.resumeFailures)return false;
    }
    ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!ready||!stop||!wake)return false;
    HANDLE thread=CreateThread(nullptr,0,Listen,nullptr,0,nullptr);
    if(!thread)return false;
    const bool ok=WaitForSingleObject(ready,2000)==WAIT_OBJECT_0&&captureInstalled.load(std::memory_order_acquire);
    if(!ok)SetEvent(stop);
    CloseHandle(thread);
    if(!ok)return false;
    enabled.store(true,std::memory_order_release);
    Log("EventSkipBuffer ordered keyboard installed keys=%u,%u,%u source=async_state_1ms modal_only=1",keys[0],keys[1],keys[2]);
    return true;
}
}
