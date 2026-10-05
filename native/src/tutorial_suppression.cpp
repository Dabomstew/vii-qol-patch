#include "patch.hpp"
#include "code_calls.hpp"
#include "tutorial_suppression.hpp"
#include <cstring>

namespace vii {
namespace {
TutorialSuppressionAdapter adapter;
uint32_t __cdecl StorySeen(uint32_t id) { return adapter.Query(id, "event"); }
uint32_t __cdecl CatalogSeen(uint32_t id) { return adapter.Query(id, "catalog"); }
}

uint32_t TutorialSuppressionAdapter::Query(uint32_t id, const char* kind) {
    const auto seen = original(id);
    if (!enabled.load(std::memory_order_acquire) || !id || id > 0xffff || (seen & 0xff)) return seen;
    // Only automatic eligibility callers see this result. The database query,
    // saved open-item list, manual catalog and quick-help paths are unchanged.
    if (trace && samples.fetch_add(1, std::memory_order_relaxed) < 128)
        Log("SuppressTutorials kind=%s id=%u original_seen=0 effective_seen=1", kind, id);
    return seen | 1;
}

bool InstallSuppressTutorials(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    const unsigned char query[] = {0x55,0x8b,0xec,0x56,0x8b,0x75,0x08,0x85,0xf6,0x74,0x60,0xf7,0xc6,0x00,0x00,0xff,0xff,0x75,0x58,0x56};
    const CallSite sites[] = {
        {reinterpret_cast<unsigned char*>(base + 0xa1ed9), {0xe8,0x62,0xe6,0x01,0x00,0x83,0xc4,0x04}, reinterpret_cast<void*>(StorySeen)},
        {reinterpret_cast<unsigned char*>(base + 0xc07c1), {0xe8,0x7a,0xfd,0xff,0xff,0x83,0xc4,0x04}, reinterpret_cast<void*>(CatalogSeen)}
    };
    if (std::memcmp(reinterpret_cast<void*>(base + 0xc0540), query, sizeof(query))) {
        Log("SuppressTutorials original predicate mismatch"); return false;
    }
    for (const auto& site : sites) {
        if (std::memcmp(site.address, site.expected.data(), site.expected.size())) {
            Log("SuppressTutorials call mismatch"); return false;
        }
    }
    adapter.original = reinterpret_cast<HelpSeenCheck>(base + 0xc0540);
    adapter.trace = Option(context, L"SuppressTutorials", L"Trace", 0) != 0;
    for (const auto& site : sites) {
        const auto report = ReplaceCall(site);
        Log("SuppressTutorials hook=%s installed=%d rollback=%d peers=%u resume_failures=%u",
            CallStatusName(report.status), report.installed, report.rolledBack, report.peers, report.resumeFailures);
        if (!report.installed || report.status != CallStatus::Installed || !report.protectionRestored || report.resumeFailures) return false;
    }
    // Partial installations remain resident and preserve original query results.
    adapter.enabled.store(true, std::memory_order_release);
    return true;
}
}
