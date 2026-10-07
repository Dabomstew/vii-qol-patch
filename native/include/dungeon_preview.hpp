#pragma once
#include "patch.hpp"
#include <atomic>

namespace vii {
using PreviewPrepare = void(__thiscall*)(void*,void*);
struct DungeonPreviewAdapter {
    PreviewPrepare original = nullptr;
    std::atomic<bool> enabled{false};
    void Prepare(void* data, void* media);
};
}
