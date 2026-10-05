#include "patch.hpp"
#include "code_calls.hpp"
#include "event_skip_buffer.hpp"
#include "ordered_event_keys.hpp"
#include <cstring>

namespace vii {
namespace {
EventBufferAdapter adapter;
template<class T> T& Field(void* p, uintptr_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) + offset);
}
int __cdecl Update(void* manager, unsigned command, void* task) {
    if (command==0||command==1) ResetOrderedEventKeys();
    return adapter.Update(manager, command, task);
}
int __fastcall Query(void* device, void*, unsigned mask) { return adapter.Query(device, mask); }
bool Eligible(void* controller, EventCallback supported) {
    return controller && Field<EventCallback>(controller, 0xa30) == supported &&
        Field<unsigned>(controller, 0x22c) && !(Field<unsigned>(controller, 0x20) & 0x10) &&
        !(Field<unsigned>(controller, 0x1c) & 0x380) && !Field<void*>(controller, 0x64);
}
}

int EventBufferAdapter::Update(void* manager, unsigned command, void* task) {
    if (enabled.load(std::memory_order_acquire)) {
        // Creation/destruction reset before the engine can allocate/free state.
        // Observe only the normal update, while task+24 remains owned and live.
        if (command == 0 || command == 1) {
            pending = false; setup = nullptr; controller = nullptr; scene = 0;
        } else if (command == 2) {
            Observe(task ? Field<void*>(task, 0x24) : nullptr);
        }
    }
    return update(manager, command, task);
}

void EventBufferAdapter::Observe(void* state) {
    if (!enabled.load(std::memory_order_acquire)) return;
    auto current = active();
    const auto phase = state ? Field<unsigned char>(state, 0x375c) : 0;
    const auto currentScene = state ? Field<unsigned>(state, 0x10) : 0;
    if (state != setup || current != controller || currentScene != scene || phase < 2 || phase > 7 ||
        !Eligible(current, supported)) pending = false;
    setup = state; controller = current; scene = currentScene;
    // Only initialization phases buffer input. Once normal input starts, the
    // engine handles fresh presses, including presses after a cancelled prompt.
    if (phase >= 2 && phase <= 6 && Eligible(current, supported) && input && *input &&
        edge(*input, Field<unsigned>(current, 0x22c)) && !pending) {
        pending = true;
        if (trace) Log("EventSkipBuffer captured scene=%u phase=%u controller=%08x", scene, phase,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(controller)));
    }
}

int EventBufferAdapter::Query(void* device, unsigned mask) {
    const int physical = edge(device, mask);
    if (!enabled.load(std::memory_order_acquire) || !pending || !setup || active() != controller ||
        !Eligible(controller, supported) || !input || device != *input ||
        mask != Field<unsigned>(controller, 0x22c) || Field<unsigned char>(setup, 0x375c) != 7 ||
        (Field<unsigned>(controller, 0x1c) & 1)) return physical;
    // The original caller now performs all ordinary prompt construction. Clear
    // before returning so cancellation cannot replay the same request.
    pending = false;
    if (trace) Log("EventSkipBuffer replay scene=%u physical=%d controller=%08x", scene, physical,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(controller)));
    return physical ? physical : 1;
}

bool InstallEventSkipBuffer(const Context& context) {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    const unsigned char entry[] = {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x10,0x56};
    unsigned char edge[] = {0x55,0x8b,0xec,0x8b,0x55,0x08,0xa1,0,0,0,0,0x3b,0xd0,0x75,0x09,0x80,0x3d,0,0,0,0,0x00,0x75,0x05,0x8b,0x41,0x1c,0x23,0xc2,0x5d,0xc2,0x04,0x00};
    auto confirm = base + 0x47c9a44, overrideFlag = base + 0x6efdb0;
    std::memcpy(edge + 7, &confirm, 4); std::memcpy(edge + 17, &overrideFlag, 4);
    if (std::memcmp(reinterpret_cast<void*>(base + 0x8e5a0), entry, sizeof(entry)) ||
        std::memcmp(reinterpret_cast<void*>(base + 0x3a2060), edge, sizeof(edge))) {
        Log("EventSkipBuffer original target mismatch"); return false;
    }

    adapter.edge = reinterpret_cast<EventInputEdge>(base + 0x3a2060);
    adapter.active = reinterpret_cast<ActiveEvent>(base + 0x331cd0);
    adapter.supported = reinterpret_cast<EventCallback>(base + 0x91600);
    adapter.input = reinterpret_cast<void**>(base + 0x47c9a40);
    adapter.trace = Option(context, L"EventSkipBuffer", L"Trace", 0) != 0;
    const CallSite sites[] = {
        {reinterpret_cast<unsigned char*>(base + 0x8e5a0), {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x10,0x56}, reinterpret_cast<void*>(Update)},
        {reinterpret_cast<unsigned char*>(base + 0x332535), {0xe8,0x26,0xfb,0x06,0x00,0x85,0xc0,0x74}, reinterpret_cast<void*>(Query)}
    };
    // Publish behavior only after both independently guarded sites are installed.
    // A partial installation retains pass-through adapters for the process life.
    for (const auto& site : sites) {
        if (std::memcmp(site.address, site.expected.data(), site.expected.size())) {
            Log("EventSkipBuffer call mismatch"); return false;
        }
    }
    // Relocate four complete position-independent instructions (seven bytes).
    // The entry jump overwrites five bytes; its tail is never executed in place.
    auto trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, entry, 7); trampoline[7] = 0xe9;
    const auto relative = static_cast<uint32_t>(base + 0x8e5a7 - reinterpret_cast<uintptr_t>(trampoline + 12));
    std::memcpy(trampoline + 8, &relative, 4);
    DWORD old = 0;
    if (!VirtualProtect(trampoline, 4096, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), trampoline, 12)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    adapter.update = reinterpret_cast<EventUpdate>(trampoline);
    for (unsigned index = 0; index < 2; ++index) {
        const auto report = index == 0 ? ReplaceEntryJump(sites[index]) : ReplaceCall(sites[index]);
        Log("EventSkipBuffer hook=%s installed=%d rollback=%d peers=%u resume_failures=%u",
            CallStatusName(report.status), report.installed, report.rolledBack, report.peers, report.resumeFailures);
        if (!report.installed || report.status != CallStatus::Installed || !report.protectionRestored || report.resumeFailures) return false;
    }
    adapter.enabled.store(true, std::memory_order_release);
    return InstallOrderedEventKeys(context);
}
}
