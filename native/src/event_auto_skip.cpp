#include "patch.hpp"
#include "code_calls.hpp"
#include "event_auto_skip.hpp"
#include <cstring>

namespace vii {
namespace {
EventSkipAdapter adapter;
template<class T> T& Field(void* object, uintptr_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
int __fastcall PollEventSkip(void* slot) { return adapter.Poll(slot); }
bool OriginalEntries(uintptr_t base) {
    const unsigned char poll[] = {0x8b,0x01,0x85,0xc0,0x74,0x0a,0x6a,0x00,0x6a,0x12,0xff,0xd0,0x83,0xc4,0x08,0xc3};
    const unsigned char callback[] = {0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x08,0x33,0xf6,0x8d,0x47,0xff,0x83,0xf8};
    unsigned char active[] = {0xa1,0,0,0,0,0x85,0xc0,0x75,0x01,0xc3,0x8b,0x0d,0,0,0,0};
    const auto handle = base + 0x007ae3e0, manager = base + 0x047c9a88;
    std::memcpy(active + 1, &handle, 4); std::memcpy(active + 12, &manager, 4);
    return !std::memcmp(reinterpret_cast<void*>(base + 0x00335ce0), poll, sizeof(poll)) &&
        !std::memcmp(reinterpret_cast<void*>(base + 0x00091600), callback, sizeof(callback)) &&
        !std::memcmp(reinterpret_cast<void*>(base + 0x00331cd0), active, sizeof(active));
}
}

int EventSkipAdapter::Poll(void* slot) const {
    if (!enabled.load(std::memory_order_acquire)) return original(slot);
    // Restrict to the verified ADV callback and current controller. Unknown
    // controllers retain their original skip-input behavior.
    auto controller = active();
    if (!controller || reinterpret_cast<uintptr_t>(slot) != reinterpret_cast<uintptr_t>(controller) + 0xa30 ||
        Field<EventCallback>(controller, 0xa30) != supported ||
        !Field<uintptr_t>(controller, 0x22c) || (Field<uint32_t>(controller, 0x20) & 0x10) ||
        Field<uintptr_t>(controller, 0x64) || (Field<uint32_t>(controller, 0x1c) & 0x381))
        return original(slot);

    // Run the exact full callback used by Yes, including ADV-specific cleanup.
    // Command C sets the engine finish flag and schedules normal teardown.
    // Returning handled prevents later input/script work in this update.
    const int handled = supported(0xc, nullptr);
    if (trace) Log("AutoSkipEvents command=C controller=%08x handled=%d flags=%08x",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(controller)), handled,
        Field<uint32_t>(controller, 0x1c));
    return handled;
}

bool InstallAutoSkipEvents(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    if (!OriginalEntries(base)) { Log("AutoSkipEvents original target mismatch; preserving existing hooks"); return false; }
    adapter.original = reinterpret_cast<EventPoll>(base + 0x00335ce0);
    adapter.active = reinterpret_cast<ActiveEvent>(base + 0x00331cd0);
    adapter.supported = reinterpret_cast<EventCallback>(base + 0x00091600);
    adapter.trace = Option(context, L"AutoSkipEvents", L"Trace", 0) != 0;
    const CallSite site{reinterpret_cast<unsigned char*>(base + 0x00335c54),
        {0xe8, 0x87, 0x00, 0x00, 0x00, 0x85, 0xc0, 0x75},
        reinterpret_cast<void*>(PollEventSkip)};
    const auto report = ReplaceCall(site);
    const bool ready = report.installed && report.status == CallStatus::Installed &&
        report.protectionRestored && !report.resumeFailures;
    if (ready) adapter.enabled.store(true, std::memory_order_release);
    Log("AutoSkipEvents hook=%s installed=%d rollback=%d peers=%u resume_failures=%u",
        CallStatusName(report.status), report.installed, report.rolledBack, report.peers, report.resumeFailures);
    return ready;
}
}
