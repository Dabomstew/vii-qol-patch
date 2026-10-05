#pragma once
#include "pac_archive.hpp"
#include "loose_manifest.hpp"
#include <functional>

namespace vii::unpack {
struct Progress { uint64_t files=0,totalFiles=0,bytes=0,totalBytes=0; std::wstring current; };
using Report=std::function<void(const Progress&)>;
struct Result { uint64_t files=0,bytes=0,reused=0; std::filesystem::path output; };
std::filesystem::path FindContents(const std::filesystem::path& selected);
// A game/CONTENTS selection discovers sibling DLC PACs; arbitrary PAC folders
// remain standalone. Missing or empty DLC returns an empty path silently.
std::filesystem::path FindDlc(const std::filesystem::path& selected);
Result Run(const std::filesystem::path& source,const std::filesystem::path& output,const Report& report={},const pac::Cancel& cancel={});
Result Verify(const std::filesystem::path& output,const Report& report={},const pac::Cancel& cancel={});
}
