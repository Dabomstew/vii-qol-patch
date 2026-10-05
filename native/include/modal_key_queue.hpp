#pragma once
#include <array>
#include <cstdint>

namespace vii {
// Caller serializes producer and consumer. No engine pointers cross threads.
class ModalKeyQueue {
    struct Event { unsigned key; uint64_t time; };
    std::array<Event,32> events{};
    unsigned head=0,count=0,held=0,claimed=0;
    bool armed=false,failed=false,served=false;
    unsigned captures=0,replays=0;
    uint64_t tick=0;
    std::array<uint64_t,4> replayTick{};
public:
    bool IsOpen() const { return armed; }
    unsigned Captures() const { return captures; }
    unsigned Replays() const { return replays; }
    void Close() { head=count=claimed=0; armed=failed=served=false; }
    void Open() { Close(); captures=replays=0;tick=0;replayTick.fill(0);armed=true; }
    void Open(unsigned initiallyHeld) { Open();held=initiallyHeld; }
    void Tick() { served=false;++tick; }
    void Key(unsigned key,bool down,uint64_t now) {
        const auto bit=1u<<key;
        if (!down) { held&=~bit; return; }
        if (held&bit) return; // No OS autorepeat or a held key across dialogs.
        held|=bit;
        if (!armed||failed) return;
        claimed|=bit;
        ++captures;
        if (count==events.size()) { count=claimed=0; failed=true; return; }
        events[(head+count++)%events.size()]={key,now};
    }
    void Sample(unsigned state,uint64_t now) {
        const unsigned fresh=state&~held;
        if(armed&&(fresh&(fresh-1))) { // Ordering inside one sample is unknown.
            count=claimed=0;failed=true;held=state;return;
        }
        for(unsigned i=0;i<4;++i)Key(i,(state&(1u<<i))!=0,now);
    }
    int Query(unsigned key,int physical,uint64_t now,bool repeat=false,bool physicalHeld=false) {
        if (!armed) return physical;
        if (count && now-events[head].time>2000) { count=claimed=0; failed=true; }
        if (failed) return physical; // Preserve ordinary input until this modal closes.
        bool queued=false;
        for(unsigned i=0;i<count;++i)if(events[(head+i)%events.size()].key==key)queued=true;
        // Give physical input the action back after both sources are released
        // and a later engine update has observed that release.
        if(!queued&&!(held&(1u<<key))&&!physical&&!physicalHeld&&tick>replayTick[key])claimed&=~(1u<<key);
        // While keyboard owns an action, its physical polling
        // cannot duplicate/reorder the captured transitions. Other actions pass.
        if (!(claimed&(1u<<key))) return physical;
        if (!repeat && !failed && !served && count && events[head].key==key) {
            head=(head+1)%events.size(); --count; served=true; ++replays;replayTick[key]=tick;return 1;
        }
        return 0;
    }
};
}
