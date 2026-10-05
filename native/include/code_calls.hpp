#pragma once
#include "patch.hpp"

namespace vii {
struct CallSite {
    unsigned char* address;
    std::array<unsigned char, 8> expected; // E8 rel32 plus three verified context bytes
    void* replacement;
};
enum class CallStatus {
    Installed, InvalidSites, UnsupportedProtection, ByteMismatch, QueryUnavailable,
    AllocationFailed, CensusFailed, ThreadLimit, OpenThreadFailed, SuspendFailed,
    ContextFailed, InstructionBusy, ThreadsChanged, ProtectFailed, WriteFailed,
    FlushFailed, RestoreFailed, RollbackFailed, ResumeFailed
};
struct CallReport {
    CallStatus status = CallStatus::InvalidSites;
    unsigned peers = 0, suspended = 0, resumeFailures = 0;
    bool installed = false, wrote = false, rolledBack = false;
    bool protectionRestored = true;
    uint64_t frozenMilliseconds = 0;
};
// Exactly two disjoint cdecl calls on one RX page. Allocations and handle
// opening precede suspension. No loader/heap/log work runs while frozen.
// Replacement code and its initialized state must remain alive after any write.
CallReport ReplaceTwoCalls(const std::array<CallSite, 2>& sites) noexcept;
// One E8 rel32 call; ABI belongs to the adapter. May cross one page boundary
// only within a single uniform committed RX region. Same transactional guards.
CallReport ReplaceCall(const CallSite& site) noexcept;
// Entry detour: caller relocates complete position-independent instructions
// covering all five overwritten bytes into an RX trampoline beforehand.
// The trampoline resumes after those complete instructions (possibly >5 bytes).
// Trampoline and adapter state must remain alive after any write.
CallReport ReplaceEntryJump(const CallSite& site) noexcept;
const char* CallStatusName(CallStatus status) noexcept;
#ifdef VII_CODE_CALLS_TESTING
enum class CallFault { None, AfterFirstWrite, Flush, Restore, Census, Suspend, Context, ThreadsChanged };
void SetCallFault(CallFault fault) noexcept;
#endif
}
