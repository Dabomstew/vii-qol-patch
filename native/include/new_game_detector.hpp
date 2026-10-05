#pragma once
#include <cstddef>
#include <cstdint>

namespace vii {
constexpr uint32_t NewGameBridgeMagic0 = 0x37494956u; // bytes: 56 49 49 37
constexpr uint32_t NewGameBridgeMagic1 = 0x4547414Du; // bytes: 4d 41 47 45

// This layout is part of the read-only ASL bridge. Keep the marker first and
// the mutable fields at these offsets; ASL locates the marker by signature.
struct NewGameBridgeState {
    uint32_t magic0;
    uint32_t magic1;
    volatile uint32_t sequence;
    volatile uint32_t transition;
};
static_assert(offsetof(NewGameBridgeState, sequence) == 8);
static_assert(offsetof(NewGameBridgeState, transition) == 12);

bool InstallNewGameDetector(const struct Context& context);
}
