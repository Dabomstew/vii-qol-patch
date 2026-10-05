#pragma once
#include "pac_unpack.hpp"
namespace vii::prepare {
using Progress=unpack::Progress;
using Report=unpack::Report;
struct Result {
    uint64_t files=0,bytes=0,reused=0,generated=0,unsupported=0,failed=0;
    std::filesystem::path output,log;
};
// Scan all files for supported standalone/embedded texture sources. Uses the
// exact x86 runtime/device recipe shared with the native hook.
Result Build(const std::filesystem::path& assets,const std::filesystem::path& cache,
    const Report& report={},const pac::Cancel& cancel={},
    const std::filesystem::path& d3dxPath={},
    const std::filesystem::path& dlcAssets={});
}
