#pragma once
#include "event_auto_skip.hpp"

namespace vii {
using EventUpdate = int (__cdecl*)(void*, unsigned, void*);
using EventInputEdge = int (__thiscall*)(void*, unsigned);
struct EventBufferAdapter {
    EventUpdate update = nullptr;
    EventInputEdge edge = nullptr;
    ActiveEvent active = nullptr;
    EventCallback supported = nullptr;
    void** input = nullptr;
    bool trace = false;
    std::atomic<bool> enabled{false};
    void* setup = nullptr;
    void* controller = nullptr;
    unsigned scene = 0;
    bool pending = false;
    void Observe(void* state);
    int Update(void* manager, unsigned command, void* task);
    int Query(void* device, unsigned mask);
};
}
