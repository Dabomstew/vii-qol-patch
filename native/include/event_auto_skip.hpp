#pragma once
#include <cstdint>
#include <atomic>

namespace vii {
using EventCallback = int (__cdecl*)(unsigned command, void* argument);
using EventPoll = int (__fastcall*)(void* callbackSlot);
using ActiveEvent = void* (__cdecl*)();
// The engine passes controller+A30 in ECX at its guarded skip-input poll.
struct EventSkipAdapter {
    EventPoll original = nullptr;
    ActiveEvent active = nullptr;
    EventCallback supported = nullptr;
    bool trace = false;
    std::atomic<bool> enabled{false};
    int Poll(void* callbackSlot) const;
};
}
