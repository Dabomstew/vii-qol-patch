#include "code_calls.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace vii;
static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)
using Fn = int (__cdecl*)(int, int, int, int);
static std::atomic<unsigned> originalCalls{0}, replacementCalls{0};
static int __cdecl Original(int a, int b, int c, int d) { ++originalCalls; return 100 + a + 3*b + 7*c + 11*d; }
static int __cdecl Replacement(int a, int b, int c, int d) { ++replacementCalls; return 200 + a + 3*b + 7*c + 11*d; }
static std::atomic<bool> quit{false}, badReturn{false};
static std::atomic<unsigned> workerCalls{0};
struct Thunks { Fn first, second; };
static DWORD WINAPI Worker(void* context) {
    const auto& f = *static_cast<Thunks*>(context);
    while (!quit) {
        const int a = f.first(1, 2, 3, 4), b = f.second(1, 2, 3, 4);
        if ((a != 172 && a != 272) || (b != 172 && b != 272)) badReturn = true;
        ++workerCalls;
        Sleep(0);
    }
    return 0;
}
static DWORD WINAPI NoWork(void*) { return 0; }
static void Encode(unsigned char* code, Fn target) {
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(code) - 5);
    code[0] = 0xe8; std::memcpy(code + 1, &relative, sizeof(relative));
}
static Fn MakeThunk(unsigned char* code, CallSite& site) {
    code[0] = 0x90;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned char push[] = {0xff, 0x74, 0x24, 0x10};
        std::memcpy(code + 1 + i*4, push, sizeof(push));
    }
    site.address = code + 17; site.replacement = reinterpret_cast<void*>(Replacement);
    Encode(site.address, Original);
    const unsigned char tail[] = {0x83, 0xc4, 0x10, 0xc3};
    std::memcpy(site.address + 5, tail, sizeof(tail));
    std::memcpy(site.expected.data(), site.address, site.expected.size());
    return reinterpret_cast<Fn>(code);
}
static bool OriginalBytes(const std::array<CallSite, 2>& sites) {
    for (const auto& s : sites) if (std::memcmp(s.address, s.expected.data(), s.expected.size())) return false;
    return true;
}
static void CheckRX(void* address) {
    MEMORY_BASIC_INFORMATION m{};
    CHECK(VirtualQuery(address, &m, sizeof(m)) == sizeof(m));
    CHECK(m.Protect == PAGE_EXECUTE_READ);
}
static CallReport TryInstall(const std::array<CallSite, 2>& sites) {
    CallReport r;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        r = ReplaceTwoCalls(sites);
        if (r.status != CallStatus::InstructionBusy && r.status != CallStatus::ThreadsChanged) break;
        Sleep(1);
    }
    return r;
}
int main() {
    auto page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page != nullptr);
    std::array<CallSite, 2> sites{};
    Thunks thunks{MakeThunk(page + 0x20, sites[0]), MakeThunk(page + 0x60, sites[1])};
    DWORD old = 0;
    CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
    CHECK(FlushInstructionCache(GetCurrentProcess(), page, 4096));
    CHECK(thunks.first(1, 2, 3, 4) == 172 && thunks.second(1, 2, 3, 4) == 172);
    auto modified = sites; modified[0].expected[1] ^= 1;
    CHECK(ReplaceTwoCalls(modified).status == CallStatus::ByteMismatch);
    CHECK(OriginalBytes(sites));
    modified = sites; modified[1].address = modified[0].address;
    CHECK(ReplaceTwoCalls(modified).status == CallStatus::InvalidSites);
    CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READWRITE, &old));
    CHECK(ReplaceTwoCalls(sites).status == CallStatus::UnsupportedProtection);
    CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
    SetCallFault(CallFault::Census);
    CHECK(ReplaceTwoCalls(sites).status == CallStatus::CensusFailed);
    CHECK(OriginalBytes(sites));

    // A saved IP inside a call must be refused and its prior suspend count kept.
    HANDLE held = CreateThread(nullptr, 0, NoWork, nullptr, CREATE_SUSPENDED, nullptr);
    CHECK(held != nullptr);
    CONTEXT saved{}; saved.ContextFlags = CONTEXT_CONTROL;
    CHECK(GetThreadContext(held, &saved));
    CONTEXT inside = saved; inside.Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(sites[0].address));
    CHECK(SetThreadContext(held, &inside));
    auto refused = ReplaceTwoCalls(sites);
    CHECK(refused.status == CallStatus::InstructionBusy && !refused.wrote && refused.resumeFailures == 0);
    CHECK(OriginalBytes(sites));
    CHECK(SetThreadContext(held, &saved));
    CHECK(ResumeThread(held) == 1);
    CHECK(WaitForSingleObject(held, 5000) == WAIT_OBJECT_0); CloseHandle(held);

    HANDLE worker = CreateThread(nullptr, 0, Worker, &thunks, 0, nullptr);
    CHECK(worker != nullptr);
    for (unsigned i = 0; i < 1000 && workerCalls.load() < 100; ++i) Sleep(1);
    CHECK(workerCalls >= 100);
    const CallFault faults[] = {CallFault::Suspend, CallFault::Context, CallFault::ThreadsChanged,
        CallFault::AfterFirstWrite, CallFault::Flush, CallFault::Restore};
    const CallStatus expectedStatus[] = {CallStatus::SuspendFailed, CallStatus::ContextFailed, CallStatus::ThreadsChanged,
        CallStatus::WriteFailed, CallStatus::FlushFailed, CallStatus::RestoreFailed};
    size_t faultIndex = 0;
    for (const auto fault : faults) {
        SetCallFault(fault);
        CallReport r;
        // A real busy IP may reject before the injected failure is reached.
        for (unsigned retry = 0; retry < 100; ++retry) {
            r = ReplaceTwoCalls(sites);
            if (r.status != CallStatus::InstructionBusy) break;
            Sleep(1);
        }
        CHECK(!r.installed && r.status != CallStatus::Installed && r.resumeFailures == 0);
        CHECK(r.status == expectedStatus[faultIndex++]);
        const bool writes = fault == CallFault::AfterFirstWrite || fault == CallFault::Flush || fault == CallFault::Restore;
        CHECK(r.wrote == writes && r.rolledBack == writes && r.protectionRestored);
        CHECK(OriginalBytes(sites)); CheckRX(page);
        CHECK(thunks.first(1, 2, 3, 4) == 172 && thunks.second(1, 2, 3, 4) == 172);
        const auto before = workerCalls.load();
        for (unsigned i = 0; i < 1000 && workerCalls.load() == before; ++i) Sleep(1);
        CHECK(workerCalls > before);
    }
    SetCallFault(CallFault::None);
    auto installed = TryInstall(sites);
    if (!installed.installed) std::printf("installation status=%s peers=%u suspended=%u\n", CallStatusName(installed.status), installed.peers, installed.suspended);
    CHECK(installed.installed && installed.status == CallStatus::Installed && installed.resumeFailures == 0);
    CHECK(installed.peers >= 1 && installed.suspended == installed.peers && installed.protectionRestored);
    unsigned beforeStack = 0, afterStack = 0;
    __asm mov beforeStack, esp
    const int result = thunks.first(1, 2, 3, 4);
    __asm mov afterStack, esp
    CHECK(result == 272 && beforeStack == afterStack);
    CHECK(thunks.second(1, 2, 3, 4) == 272);
    CheckRX(page);
    // Reinstallation must not overwrite the first replacement.
    CHECK(ReplaceTwoCalls(sites).status == CallStatus::ByteMismatch);
    quit = true; CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0); CloseHandle(worker);
    CHECK(!badReturn && originalCalls > 0 && replacementCalls > 0);
    // Single-call users share rollback and ownership checks, with their own ABI.
    CHECK(VirtualProtect(page, 4096, PAGE_READWRITE, &old));
    CallSite single{};
    auto singleThunk = MakeThunk(page + 0x100, single);
    CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
    CHECK(FlushInstructionCache(GetCurrentProcess(), page, 4096));
    for (auto fault : {CallFault::AfterFirstWrite, CallFault::Flush, CallFault::Restore}) {
        SetCallFault(fault);
        const auto r = ReplaceCall(single);
        CHECK(!r.installed && r.wrote && r.rolledBack && r.protectionRestored);
        CHECK(std::memcmp(single.address, single.expected.data(), 8) == 0);
        CHECK(singleThunk(1, 2, 3, 4) == 172); CheckRX(page);
    }
    CHECK(ReplaceCall(single).installed);
    __asm mov beforeStack, esp
    const auto singleResult = singleThunk(1, 2, 3, 4);
    __asm mov afterStack, esp
    CHECK(singleResult == 272 && beforeStack == afterStack);
    CHECK(ReplaceCall(single).status == CallStatus::ByteMismatch);
    CheckRX(page);
    // Entry transfer preserves the caller's return address and argument stack.
    CHECK(VirtualProtect(page, 4096, PAGE_READWRITE, &old));
    auto entry = page + 0x200;
    const unsigned char prologue[] = {0x55,0x8b,0xec,0x6a,0xff,0x83,0xc4,0x04,0x5d};
    std::memcpy(entry, prologue, sizeof(prologue));
    Encode(entry + sizeof(prologue), Original); entry[sizeof(prologue)] = 0xe9;
    auto trampoline = page + 0x240;
    std::memcpy(trampoline, entry, 5);
    Encode(trampoline + 5, reinterpret_cast<Fn>(entry + 5)); trampoline[5] = 0xe9;
    CallSite jump{entry,{},reinterpret_cast<void*>(Replacement)};
    std::memcpy(jump.expected.data(),entry,8);
    CHECK(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),page,4096));
    auto entryFn = reinterpret_cast<Fn>(entry), originalFn = reinterpret_cast<Fn>(trampoline);
    CHECK(entryFn(1,2,3,4)==172 && originalFn(1,2,3,4)==172);
    for (auto fault : {CallFault::AfterFirstWrite, CallFault::Flush, CallFault::Restore}) {
        SetCallFault(fault); const auto r = ReplaceEntryJump(jump);
        CHECK(!r.installed && r.wrote && r.rolledBack && r.protectionRestored);
        CHECK(std::memcmp(entry,jump.expected.data(),8)==0);
        CHECK(entryFn(1,2,3,4)==172 && originalFn(1,2,3,4)==172); CheckRX(page);
    }
    CHECK(ReplaceEntryJump(jump).installed);
    __asm mov beforeStack, esp
    const auto jumpResult = entryFn(1,2,3,4);
    __asm mov afterStack, esp
    CHECK(jumpResult==272 && beforeStack==afterStack && originalFn(1,2,3,4)==172);
    CHECK(ReplaceEntryJump(jump).status==CallStatus::ByteMismatch); CheckRX(page);
    CHECK(VirtualFree(page, 0, MEM_RELEASE));
    // Exact battle-hook layout: E8 begins three bytes before the page end.
    auto crossing = static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(crossing);
    CallSite crossSite{};
    auto crossFn = MakeThunk(crossing+4093-17,crossSite);
    CHECK(VirtualProtect(crossing,8192,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),crossing,8192));
    CHECK(crossFn(1,2,3,4)==172);
    // Refuse mixed protection instead of applying the first page's protection
    // to a differently owned second page.
    CHECK(VirtualProtect(crossing+4096,4096,PAGE_READWRITE,&old));
    CHECK(ReplaceCall(crossSite).status==CallStatus::UnsupportedProtection);
    CHECK(std::memcmp(crossSite.address,crossSite.expected.data(),8)==0);
    CHECK(VirtualProtect(crossing+4096,4096,PAGE_EXECUTE_READ,&old));
    for (auto fault : {CallFault::AfterFirstWrite,CallFault::Flush,CallFault::Restore}) {
        SetCallFault(fault); const auto r=ReplaceCall(crossSite);
        CHECK(!r.installed && r.wrote && r.rolledBack && r.protectionRestored);
        CHECK(std::memcmp(crossSite.address,crossSite.expected.data(),8)==0);
        CHECK(crossFn(1,2,3,4)==172); CheckRX(crossing); CheckRX(crossing+4096);
    }
    CHECK(ReplaceCall(crossSite).installed);
    __asm mov beforeStack,esp
    const auto crossResult=crossFn(1,2,3,4);
    __asm mov afterStack,esp
    CHECK(crossResult==272 && beforeStack==afterStack);
    CheckRX(crossing); CheckRX(crossing+4096);
    CHECK(ReplaceCall(crossSite).status==CallStatus::ByteMismatch);
    CHECK(VirtualFree(crossing,0,MEM_RELEASE));
    std::printf("PASS: %u checks; live x86 two-call transaction, cdecl stack, peer resume, busy IP refusal, altered-byte refusal, injected rollback; %u worker iterations; frozen %llu ms\n",
        checks, workerCalls.load(), installed.frozenMilliseconds);
    return 0;
}
