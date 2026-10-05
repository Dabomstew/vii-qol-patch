#pragma once
#include <atomic>
#include <cstdint>

namespace vii {
using HelpSeenCheck = uint32_t (__cdecl*)(uint32_t);
struct TutorialSuppressionAdapter {
    HelpSeenCheck original = nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<unsigned> samples{0};
    bool trace = false;
    uint32_t Query(uint32_t id, const char* kind);
};
}
