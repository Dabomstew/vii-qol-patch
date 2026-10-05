#include "texture_writer.hpp"
#include <cassert>
#include <future>
#include <atomic>
#include <iostream>
using namespace vii;
int main(){
    std::promise<void> entered,release;auto gate=release.get_future();std::atomic<int> writes{0};
    {
        TextureWriter writer(16,[&](const TextureWriteRecord& r){
            if(++writes==1){entered.set_value();gate.wait();}
            assert(r.source[0]==17&&r.dds[0]==23&&r.recipe==1&&r.desc.Width==16);
        });
        std::vector<unsigned char> source(4,17),dds(4,23);D3D11_TEXTURE2D_DESC desc{};desc.Width=16;
        assert(writer.Enqueue(1,1,source,dds,desc));entered.get_future().wait();
        assert(writer.Enqueue(2,1,source,dds,desc));
        assert(!writer.Enqueue(3,1,source,dds,desc)); // Active + queued count toward cap.
        source[0]=99;dds[0]=99; // Queued records must own their bytes.
        release.set_value();
    }
    assert(writes==2); // Destruction drains accepted records outside loader lock.
    std::cout<<"texture writer ownership, bounds, backpressure and drain passed\n";
}
