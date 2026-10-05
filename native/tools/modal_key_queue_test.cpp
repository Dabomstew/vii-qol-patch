#include "modal_key_queue.hpp"
#include <cstdio>
#include <cstdlib>
using vii::ModalKeyQueue;
#define CHECK(x) do { if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);} } while(false)
int main() {
    ModalKeyQueue q;
    CHECK(q.Query(0,17,0)==17); // Unarmed controller/keyboard pass-through.
    q.Open(); q.Key(0,true,10); q.Key(0,false,20); q.Key(2,true,30); q.Key(2,false,40);
    CHECK(q.Query(2,64,50)==0); // Enter cannot overtake Up after a stall.
    CHECK(q.Query(0,0,50)==1);
    CHECK(q.Query(0,0x100000,50,true)==0); // Physical repeat cannot duplicate Up.
    CHECK(q.Query(2,64,50)==0); // One deliberate action per update.
    q.Tick(); CHECK(q.Query(0,0x100000,60)==0); CHECK(q.Query(2,0,60)==1);
    q.Tick(); CHECK(q.Query(2,64,70)==0); CHECK(q.Query(1,0x400000,70)==0x400000);
    q.Close(); CHECK(q.Query(2,64,80)==64);
    q.Open(); CHECK(q.Query(2,0,90)==0); // Nothing survives cancellation/scene boundary.
    q.Key(0,true,100); q.Key(0,true,110); CHECK(q.Query(0,0,120)==1);
    q.Tick(); CHECK(q.Query(0,0,130)==0); // OS autorepeat ignored.
    q.Close(); q.Open(); q.Key(0,true,140); CHECK(q.Query(0,0,150)==0); // Held across boundary.
    q.Key(0,false,160); q.Key(0,true,170); CHECK(q.Query(0,0,180)==1); q.Key(0,false,190);
    q.Close(); q.Open(); q.Key(2,true,200);q.Key(2,false,210);
    CHECK(q.Query(2,64,2201)==64); q.Tick(); CHECK(q.Query(2,64,2210)==64); // Stale actions fall back.
    q.Close();q.Open();
    for(unsigned i=0;i<33;++i){q.Key(0,true,2300+i);q.Key(0,false,2300+i);}
    CHECK(q.Query(0,0x100000,2400)==0x100000); // Overflow drops FIFO and preserves ordinary input.
    q.Close();q.Open();q.Key(3,true,2500);q.Key(3,false,2501);
    CHECK(q.Query(3,0,2510)==1);q.Close();q.Open();CHECK(q.Query(3,0,2520)==0);
    q.Close();q.Open(0);q.Tick();q.Sample(1,2600);q.Sample(0,2610);
    CHECK(q.Query(0,0,2620)==1);q.Tick();
    CHECK(q.Query(0,0,2630,false,true)==0);CHECK(q.Query(0,1,2630,true,true)==0);
    q.Tick();CHECK(q.Query(0,0,2640)==0);CHECK(q.Query(0,1,2650)==1); // Physical input regains ownership.
    q.Open(1);q.Sample(1,2700);CHECK(q.Query(0,0,2710)==0); // Arm snapshot ignores already-held key.
    q.Sample(0,2720);q.Sample(1,2730);CHECK(q.Query(0,0,2740)==1);
    q.Open(0);q.Sample(5,2800);CHECK(q.Query(0,17,2810)==17);CHECK(q.Query(2,64,2810)==64);
    CHECK(q.Replays()==0); // Same-sample ambiguous actions use ordinary input.
    std::puts("Modal keyboard ordering, physical deduplication, close, repeat, expiry and overflow pass");
}
