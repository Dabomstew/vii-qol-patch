#pragma once
#include "texture_upload.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace vii {
struct TextureWriteRecord {
    uint64_t request=0;unsigned recipe=0;
    std::vector<unsigned char> source,dds;D3D11_TEXTURE2D_DESC desc{};
    size_t Cost() const{return source.size()+dds.size();}
};
// Only owned CPU bytes cross this boundary, never a resource/context/blob.
// Outstanding bytes include the active write. Full queues use caller fallback.
class TextureWriter {
    size_t limit,bytes=0;bool closing=false;
    std::mutex mutex;std::condition_variable ready;
    std::deque<TextureWriteRecord> queue;
    std::function<void(const TextureWriteRecord&)> sink;std::thread worker;
    void Run(){for(;;){TextureWriteRecord record;
        {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return closing||!queue.empty();});
            if(queue.empty())return;record=std::move(queue.front());queue.pop_front();}
        try{sink(record);}catch(...){} // The sink reports write failures; keep draining.
        {std::lock_guard<std::mutex> lock(mutex);bytes-=record.Cost();}
    }}
public:
    TextureWriter(size_t budget,std::function<void(const TextureWriteRecord&)> write):limit(budget),sink(std::move(write)),worker([this]{Run();}){}
    ~TextureWriter(){{std::lock_guard<std::mutex> lock(mutex);closing=true;}ready.notify_one();if(worker.joinable())worker.join();}
    bool Enqueue(uint64_t request,unsigned recipe,TextureBytes source,TextureBytes dds,const D3D11_TEXTURE2D_DESC& desc){
        std::lock_guard<std::mutex> lock(mutex);
        if(closing||source.size()>limit||dds.size()>limit-source.size()||source.size()+dds.size()>limit-bytes||queue.size()>=128)return false;
        TextureWriteRecord record;record.request=request;record.recipe=recipe;record.desc=desc;
        record.source.assign(source.data(),source.data()+source.size());record.dds.assign(dds.data(),dds.data()+dds.size());
        const auto cost=record.Cost();queue.push_back(std::move(record));bytes+=cost;ready.notify_one();return true;
    }
};
}
