#pragma once
#include "texture_upload.hpp"
#include <functional>
#include <string>
#include <vector>
namespace vii {
struct TextureSource {
    size_t offset=0;
    unsigned image=0;
    bool singleMip=false;
    std::vector<unsigned char> dds;
    std::string issue;
};
void ScanTextureSources(TextureBytes bytes,const std::function<void(TextureSource&&)>& emit);
}
