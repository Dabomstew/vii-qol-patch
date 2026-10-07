#include "texture_disk.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
namespace vii {
bool InstallNeptasm(const Context&){return false;}
bool InstallNewGameDetector(const Context&){return false;}
bool InstallLoadTiming(const Context&){return false;}
bool InstallSuppressDungeonPreview(const Context&){return false;}
bool InstallJPBattleBalance(const Context&){return false;}
bool InstallMipCache(const Context&){return false;} bool InstallMotionCache(const Context&){return false;}
bool InstallLooseFiles(const Context&){return false;} bool InstallAutoSkipEvents(const Context&){return false;} bool InstallEventSkipBuffer(const Context&){return false;} bool InstallSuppressTutorials(const Context&){return false;} bool InstallBattleAutoSkip(const Context&){return false;} void ProbeStartup(const Context&)noexcept{}
}
int wmain(int argc,wchar_t** argv) {
    using namespace vii;if(argc!=2)return 2;std::wstring path=argv[1];
    if(!CreateDirectoryW(path.c_str(),nullptr)){std::cerr<<"test requires a new directory\n";return 2;}
    std::vector<unsigned char> source(160);auto word=[&](size_t at,uint32_t n){memcpy(source.data()+at,&n,4);};
    word(0,0x20534444);word(4,124);word(12,8);word(16,8);word(28,1);word(76,32);word(84,0x31545844);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_BC1_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    Digest identity{};identity[0]=9;TextureDisk disk;std::vector<unsigned char> out;TextureUpload upload;
    assert(!disk.Open(L"relative",identity));
    assert(disk.Open(path,identity));
    assert(!disk.Read(source,out,upload));assert(disk.Write(source,source,desc));
    assert(disk.Write(source,source,desc));assert(disk.Read(source,out,upload));assert(out==source);
    assert(upload.desc.Width==8 && upload.slices.size()==1);
    TextureDisk other;assert(!other.Open(path,identity)); // exclusive bounded writer
    auto changed=source;changed.back()=1;assert(!disk.Read(changed,out,upload));
    auto wrong=identity;wrong[0]=10;assert(disk.Open(path,wrong));assert(!disk.Read(source,out,upload));
    assert(disk.Open(path,identity));
    for(unsigned i=0;i<20;++i){changed.back()=static_cast<unsigned char>(i);assert(disk.Write(changed,source,desc));}
    assert(disk.Read(source,out,upload));
    auto file=disk.Path(source);
    {std::fstream f(file,std::ios::binary|std::ios::in|std::ios::out);f.seekp(100);f.put('x');}
    assert(!disk.Read(source,out,upload)); // whole-record checksum/exact source corruption
    {std::ofstream f(file,std::ios::binary|std::ios::trunc);f.put('x');}
    assert(!disk.Read(source,out,upload)); // truncated record
    assert(disk.Write(source,source,desc));assert(disk.Read(source,out,upload));assert(out==source);
    {std::ofstream f(file+L".part");f<<"interrupted";}
    assert(disk.Read(source,out,upload));assert(disk.Write(source,source,desc));
    auto invalid=desc;invalid.Width=9;assert(!disk.Write(source,source,invalid));assert(disk.Read(source,out,upload));
    std::cout<<"Texture disk guards passed: roundtrip, exact source, identity, exclusivity, unlimited retention, corruption/truncation repair, interrupted publication, invalid upload rejection\n";
}
