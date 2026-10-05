#include "patch.hpp"
#include "event_skip_buffer.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace vii {
void Log(const char*, ...) noexcept {}
int Option(const Context&, const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
using namespace vii;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
alignas(4) static std::array<unsigned char, 0x3760> state{};
alignas(4) static std::array<unsigned char, 0xa38> controller{};
alignas(4) static std::array<unsigned char, 0x24> input{};
template<class T> T& Field(void* p, unsigned offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset); }
static void* current = controller.data();
static void* device = input.data();
static void* __cdecl Active() { return current; }
static int __cdecl Supported(unsigned, void*) { CHECK(false); return 0; }
static int __fastcall Edge(void* p, void*, unsigned mask) { return Field<unsigned>(p, 0x1c) & mask; }
static EventBufferAdapter* testing = nullptr;
static int __cdecl OriginalUpdate(void*, unsigned command, void*) {
    if (command == 0 || command == 1) CHECK(!testing->pending && !testing->setup);
    return 91;
}
static void Reset(EventBufferAdapter& a) {
    state.fill(0); controller.fill(0); input.fill(0); current = controller.data(); device = input.data();
    Field<unsigned>(state.data(), 0x10) = 100;
    Field<unsigned char>(state.data(), 0x375c) = 6;
    Field<EventCallback>(controller.data(), 0xa30) = Supported;
    Field<unsigned>(controller.data(), 0x22c) = 1;
    a.Observe(nullptr);
}
static void Capture(EventBufferAdapter& a) {
    Field<unsigned>(input.data(), 0x1c) = 1;
    const auto before = input;
    a.Observe(state.data());
    CHECK(a.pending && input == before);
    Field<unsigned>(input.data(), 0x1c) = 0;
}
int main() {
    static_assert(sizeof(void*) == 4, "x86 only");
    EventBufferAdapter a; a.active = Active; a.supported = Supported; a.input = &device;
    a.edge = reinterpret_cast<EventInputEdge>(Edge);
    a.update = OriginalUpdate; testing = &a;
    Reset(a); Field<unsigned>(input.data(), 0x1c) = 1;
    a.Observe(state.data()); CHECK(!a.pending && a.Query(device, 1) == 1);
    a.enabled.store(true); Reset(a);
    for (unsigned phase = 2; phase <= 6; ++phase) {
        Reset(a); Field<unsigned char>(state.data(), 0x375c) = static_cast<unsigned char>(phase);
        Capture(a); a.Observe(state.data()); CHECK(a.pending);
        CHECK(a.Query(device, 1) == 0 && a.pending); // Still initializing.
        Field<unsigned char>(state.data(), 0x375c) = 7;
        const auto c = controller;
        const auto s = state;
        const auto i = input;
        CHECK(a.Query(device, 1) == 1 && !a.pending);
        CHECK(controller == c && state == s && input == i);
        CHECK(a.Query(device, 1) == 0); // Cancel/re-entry cannot replay.
    }
    for (unsigned phase : {0u, 1u, 8u}) {
        Reset(a); Capture(a); Field<unsigned char>(state.data(), 0x375c) = static_cast<unsigned char>(phase);
        a.Observe(state.data()); CHECK(!a.pending);
    }
    Reset(a); Capture(a); Field<unsigned>(state.data(), 0x10) = 101; a.Observe(state.data()); CHECK(!a.pending);
    Reset(a); Capture(a); current = nullptr; a.Observe(state.data()); CHECK(!a.pending);
    Reset(a); Capture(a); a.Observe(nullptr); CHECK(!a.pending);
    Reset(a); Capture(a); Field<EventCallback>(controller.data(), 0xa30) = nullptr; a.Observe(state.data()); CHECK(!a.pending);
    for (unsigned flag : {0x80u, 0x100u, 0x200u}) {
        Reset(a); Capture(a); Field<unsigned>(controller.data(), 0x1c) = flag; a.Observe(state.data()); CHECK(!a.pending);
    }
    Reset(a); Capture(a); Field<unsigned>(controller.data(), 0x20) = 0x10; a.Observe(state.data()); CHECK(!a.pending);
    Reset(a); Capture(a); Field<void*>(controller.data(), 0x64) = device; a.Observe(state.data()); CHECK(!a.pending);
    Reset(a); Field<unsigned char>(state.data(), 0x375c) = 7; Field<unsigned>(input.data(), 0x1c) = 1;
    a.Observe(state.data()); CHECK(!a.pending && a.Query(device, 1) == 1); // Normal press.
    Reset(a); Capture(a); Field<unsigned char>(state.data(), 0x375c) = 7;
    CHECK(a.Query(device, 2) == 0 && a.pending);
    Field<unsigned>(input.data(), 0x1c) = 2; CHECK(a.Query(device, 2) == 2 && a.pending);
    Field<unsigned>(input.data(), 0x1c) = 0; Field<unsigned>(controller.data(), 0x1c) = 1;
    CHECK(a.Query(device, 1) == 0 && a.pending);
    Field<unsigned>(controller.data(), 0x1c) = 0; CHECK(a.Query(device, 1) == 1 && !a.pending);
    for (unsigned command : {0u, 1u}) {
        Reset(a); Capture(a); CHECK(a.Update(nullptr, command, nullptr) == 91 && !a.pending);
    }
    // Execute the exact seven displaced entry bytes through a trampoline. The
    // remaining synthetic body returns the third argument loaded into EBX.
    auto code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(code != nullptr);
    const unsigned char body[] = {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x10,0x8b,0xc3,0x5b,0x5d,0xc3};
    std::memcpy(code, body, sizeof(body));
    std::memcpy(code + 32, body, 7); code[39] = 0xe9;
    const auto rel = static_cast<uint32_t>((code + 7) - (code + 44)); std::memcpy(code + 40, &rel, 4);
    DWORD old = 0; CHECK(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old));
    CHECK(FlushInstructionCache(GetCurrentProcess(), code, 44));
    a.update = reinterpret_cast<EventUpdate>(code + 32);
    unsigned before = 0, after = 0;
    __asm mov before, esp
    const auto result = a.Update(nullptr, 99, reinterpret_cast<void*>(0x12345678));
    __asm mov after, esp
    CHECK(result == 0x12345678 && before == after);
    CHECK(VirtualFree(code, 0, MEM_RELEASE));
    std::printf("PASS: %u checks; startup capture/release, prompt replay, cancellation, lifecycle, guards, immutable engine state\n", checks);
    return 0;
}
