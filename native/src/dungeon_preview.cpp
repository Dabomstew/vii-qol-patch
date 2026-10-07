#include "dungeon_preview.hpp"
#include "code_calls.hpp"
#include <cstring>

namespace vii {
namespace {
DungeonPreviewAdapter adapter;
void __fastcall Prepare(void* data,void*,void* media) { adapter.Prepare(data,media); }
bool MakeTrampoline(unsigned char* entry) {
    // Whole, position-independent instructions: PUSH EBP; MOV EBP,ESP; PUSH -1.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code)return false;
    std::memcpy(code,entry,5);code[5]=0xe9;
    const auto rel=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry+5)-reinterpret_cast<uintptr_t>(code+10));
    std::memcpy(code+6,&rel,4);
    DWORD prior=0;
    if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&prior)||!FlushInstructionCache(GetCurrentProcess(),code,10)) {
        VirtualFree(code,0,MEM_RELEASE);return false;
    }
    adapter.original=reinterpret_cast<PreviewPrepare>(code);return true;
}
}
void DungeonPreviewAdapter::Prepare(void* data,void* media) {
    auto* fields=static_cast<uint32_t*>(data);
    // Only an empty DungeonInfoWindow media owner can be suppressed. Unexpected
    // existing handles retain the complete original update/cleanup lifecycle.
    if(enabled.load(std::memory_order_acquire)&&fields&&!fields[3]&&!fields[5]) {
        fields[10]=2;fields[11]=fields[12]=0;return;
    }
    original(data,media);
}
bool InstallSuppressDungeonPreview(const Context& context) {
    const auto base=reinterpret_cast<uintptr_t>(context.game);
    auto* entry=reinterpret_cast<unsigned char*>(base+0x218fc0);
    std::array<unsigned char,16> signature{0x55,0x8b,0xec,0x6a,0xff,0x68,0,0,0,0,0x64,0xa1,0,0,0,0};
    const auto handler=static_cast<uint32_t>(base+0x49b9ee);
    std::memcpy(signature.data()+6,&handler,4);
    if(std::memcmp(entry,signature.data(),signature.size())||!MakeTrampoline(entry)) {
        Log("SuppressDungeonPreview signature/trampoline unavailable");return false;
    }
    std::array<unsigned char,8> expected{};std::memcpy(expected.data(),signature.data(),8);
    const auto report=ReplaceEntryJump({entry,expected,reinterpret_cast<void*>(Prepare)});
    Log("SuppressDungeonPreview hook=%s installed=%d rollback=%d peers=%u resume_failures=%u",
        CallStatusName(report.status),report.installed,report.rolledBack,report.peers,report.resumeFailures);
    if(!report.installed||report.status!=CallStatus::Installed||!report.protectionRestored||report.resumeFailures)return false;
    adapter.enabled.store(true,std::memory_order_release);return true;
}
}
