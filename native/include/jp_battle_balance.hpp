#pragma once
#include "patch.hpp"

namespace vii::jp_balance {
struct Tier { uint32_t threshold, percent; };
uint32_t HitBonus(uint32_t count, uint32_t* threshold) noexcept;
uint32_t CreditBonus(uint32_t count, uint32_t* threshold) noexcept;
bool HighLevel(uint32_t flags, uint16_t level) noexcept;
float AttackComponent(float power, float stat, float tec, uint8_t category) noexcept;
struct Site {
    uintptr_t rva;
    std::array<unsigned char, 8> signature;
    size_t displaced;
};
inline constexpr std::array<Site, 4> Sites = {{
    {0x18f550, {0x55,0x8b,0xec,0x8b,0x45,0x08,0x3d,0xe8}, 6},
    {0x18df70, {0x55,0x8b,0xec,0x8b,0x45,0x08,0x3d,0xa0}, 6},
    {0x29ebbf, {0xf3,0x0f,0x59,0x9d,0xd4,0xfd,0xff,0xff}, 8},
    {0x2a2417, {0xf3,0x0f,0x59,0x9d,0xd0,0xfe,0xff,0xff}, 8},
}};
#ifdef VII_JP_BALANCE_TESTING
bool InstallTestSites(const std::array<unsigned char*, 4>& addresses, int failBefore = -1);
void ResetTestState(); // Only after every test mapping is no longer callable.
bool TestActive();
void* TestAdapter(size_t index);
void SetTestGate(bool enabled);
#endif
}
