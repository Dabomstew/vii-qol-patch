#pragma once
#include <array>
#include <cstdint>

namespace vii {
// Explicit partial coverage, never a claim to observe all game loading.
constexpr uint32_t LoadCoverageBattleCharacters = 1;
constexpr uint32_t LoadCoverageDungeonMap = 2;
constexpr uint32_t LoadCoverageAdvScript = 4;
constexpr uint32_t LoadCoverageWorldResources = 8;
constexpr uint32_t LoadCoverageTitleMovieSetup = 16;
constexpr uint32_t LoadCoverageSaveMetadata = 32;
constexpr uint32_t LoadCoverageKnown = LoadCoverageBattleCharacters | LoadCoverageDungeonMap | LoadCoverageAdvScript | LoadCoverageWorldResources | LoadCoverageTitleMovieSetup | LoadCoverageSaveMetadata;
enum class LoadStatus : uint32_t { Disabled = 0, ReadyPartial = 1, Fault = 2 };
enum class LoadFault : uint32_t { None, Clock, Capacity, MissingOwner, LongGap, Memory, Install };

struct LoadSample {
    LoadStatus status = LoadStatus::Disabled;
    LoadFault fault = LoadFault::None;
    uint32_t reason = 0, owners = 0, generation = 0, transitions = 0;
    bool focused = true;
    uint64_t completed = 0, opened = 0, lastQpc = 0;
};

// A single serialized writer owns this reducer. No game pointers are followed
// here. Owner/object values are identity tokens copied by the owning callback.
class LoadTimingCore {
    struct Owner { uint32_t task = 0, object = 0, reason = 0; uint64_t frame = 0, lastQpc = 0; };
    std::array<Owner, 8> owners_{};
    LoadSample value_{};
    uint64_t frame_ = 0, lastFrameQpc_ = 0, maxGap_ = 0;
    bool clock(uint64_t qpc) noexcept {
        if (qpc < value_.lastQpc) { Fail(LoadFault::Clock, value_.lastQpc); return false; }
        value_.lastQpc = qpc;
        return true;
    }
    void classify(uint64_t qpc) noexcept {
        uint32_t count = 0, reasons = 0;
        for (const auto& owner : owners_) if (owner.task) { ++count; reasons |= owner.reason; }
        const uint32_t next = value_.focused ? reasons : 0;
        value_.owners = count;
        if (next == value_.reason) return;
        if (value_.reason) value_.completed += qpc - value_.opened;
        value_.opened = next ? qpc : 0;
        value_.reason = next;
        ++value_.transitions;
    }
public:
    void Enable(uint64_t qpc, uint64_t maxFrameGap) noexcept {
        owners_ = {}; value_ = {}; frame_ = 0;
        value_.status = LoadStatus::ReadyPartial; value_.lastQpc = qpc;
        lastFrameQpc_ = qpc; maxGap_ = maxFrameGap;
    }
    const LoadSample& Sample() const noexcept { return value_; }
    uint64_t Total(uint64_t qpc) const noexcept {
        return value_.completed + (value_.reason && qpc >= value_.opened ? qpc - value_.opened : 0);
    }
    void Fail(LoadFault fault, uint64_t end) noexcept {
        if (value_.reason) {
            // Never extrapolate an unobserved gap. Retain only the confirmed
            // prefix of the open interval, then disable both consumers.
            if (end > value_.opened) value_.completed += end - value_.opened;
            ++value_.transitions;
        }
        owners_ = {}; value_.reason = value_.owners = 0; value_.opened = 0;
        value_.status = LoadStatus::Fault; value_.fault = fault;
    }
    void Frame(uint64_t qpc, bool focused) noexcept {
        if (value_.status != LoadStatus::ReadyPartial || !clock(qpc)) return;
        if (value_.reason) {
            uint64_t confirmed = qpc;
            bool missing = false;
            for (const auto& owner : owners_) if (owner.task) {
                if (owner.lastQpc < confirmed) confirmed = owner.lastQpc;
                if (owner.frame != frame_) missing = true;
            }
            if (maxGap_ && qpc - lastFrameQpc_ > maxGap_) { Fail(LoadFault::LongGap, confirmed); return; }
            if (missing) { Fail(LoadFault::MissingOwner, confirmed); return; }
        }
        if (focused != value_.focused) {
            // Re-entry after focus loss needs fresh owner-thread evidence.
            owners_ = {}; value_.focused = focused; classify(qpc);
        }
        lastFrameQpc_ = qpc; ++frame_;
    }
    void Observe(uint32_t task, uint32_t object, bool resourceWait, uint64_t qpc,
                 uint32_t reason = LoadCoverageBattleCharacters) noexcept {
        if (value_.status != LoadStatus::ReadyPartial || !clock(qpc)) return;
        if (!task || !value_.focused || (reason != LoadCoverageBattleCharacters && reason != LoadCoverageDungeonMap && reason != LoadCoverageAdvScript && reason != LoadCoverageWorldResources && reason != LoadCoverageTitleMovieSetup && reason != LoadCoverageSaveMetadata)) return;
        Owner* slot = nullptr;
        for (auto& owner : owners_) if (owner.task == task && owner.reason == reason) { slot = &owner; break; }
        if (slot && (slot->object != object || !resourceWait)) {
            *slot = {}; classify(qpc);
        }
        if (!resourceWait || !object) return;
        if (!slot) for (auto& owner : owners_) if (!owner.task) { slot = &owner; break; }
        if (!slot) { Fail(LoadFault::Capacity, qpc); return; }
        if (!slot->task) ++value_.generation;
        *slot = {task, object, reason, frame_, qpc}; classify(qpc);
    }
    void Cancel(uint32_t task, uint64_t qpc, uint32_t reason = LoadCoverageBattleCharacters) noexcept {
        Observe(task, 0, false, qpc, reason);
    }
};

// Restrict interval mutation to a verified wait and the normal dynamic clock.
inline bool CanUncapLoad(const LoadSample& sample, bool requested, bool globalUnlock,
                         uint32_t forcedTime, uint32_t interval) noexcept {
    return requested && !globalUnlock && sample.status == LoadStatus::ReadyPartial &&
        sample.focused && sample.reason != 0 && forcedTime == 0 && interval != 0;
}
}
