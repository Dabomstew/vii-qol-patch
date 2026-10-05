#pragma once
#include <atomic>
#include <cstdint>

namespace vii {
using BattleInputPoll = int (__thiscall*)(void*, unsigned);
struct BattleSkipAdapter {
    BattleInputPoll original = nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<unsigned> forced{0};
    bool trace = false;
    const char* kind = "animation";
    int Poll(void* input, unsigned mask);
};
}
