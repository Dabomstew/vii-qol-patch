#include "texture_disk.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
namespace vii {
bool InstallNeptasm(const Context&){return false;}
bool InstallNewGameDetector(const Context&){return false;}
bool InstallLoadTiming(const Context&){return false;}
bool InstallSkipBattleLoadingPresent(const Context&){return false;}
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
    auto readBoth=[&](TextureBytes input,bool expected){
        for(bool verify:{false,true})assert(disk.Read(input,out,upload,verify)==expected);
    };
    assert(!disk.Open(L"relative",identity));
    assert(disk.Open(path,identity));
    readBoth(source,false);assert(disk.Write(source,source,desc));
    assert(disk.Write(source,source,desc));assert(disk.Read(source,out,upload));assert(out==source);
    readBoth(source,true);assert(out==source);
    assert(upload.desc.Width==8 && upload.slices.size()==1);
    TextureDisk other;assert(!other.Open(path,identity)); // exclusive bounded writer
    auto changed=source;changed.back()=1;readBoth(changed,false);
    auto wrong=identity;wrong[0]=10;assert(disk.Open(path,wrong));readBoth(source,false);
    assert(disk.Open(path,identity));
    for(unsigned i=0;i<20;++i){changed.back()=static_cast<unsigned char>(i);assert(disk.Write(changed,source,desc));}
    assert(disk.Read(source,out,upload));
    auto file=disk.Path(source);
    constexpr size_t headerSize=88; // VMT2 fixture layout; no format migration.
    auto flip=[&](size_t at){
        std::fstream f(file,std::ios::binary|std::ios::in|std::ios::out);assert(f);
        f.seekg(at);char value=0;f.get(value);assert(f);f.seekp(at);f.put(value^1);assert(f);
    };
    auto setWord=[&](size_t at,uint32_t value){
        std::fstream f(file,std::ios::binary|std::ios::in|std::ios::out);assert(f);
        f.seekp(at);f.write(reinterpret_cast<const char*>(&value),sizeof(value));assert(f);
    };
    flip(headerSize+12); // stored source differs, generated DDS is intact
    assert(!disk.Read(source,out,upload)); // default stays strict for preparation
    assert(!disk.Read(source,out,upload,true));
    assert(disk.Read(source,out,upload,false));assert(out==source);
    assert(disk.Write(source,source,desc));
    flip(headerSize+source.size()*2); // digest only
    assert(!disk.Read(source,out,upload,true));
    assert(disk.Read(source,out,upload,false));assert(out==source);
    assert(disk.Write(source,source,desc));
    flip(headerSize+source.size()*2-1); // changed pixel bytes remain structurally valid
    assert(!disk.Read(source,out,upload,true));
    assert(disk.Read(source,out,upload,false));changed=source;changed.back()^=1;assert(out==changed);
    // Both modes retain namespace, lengths, descriptor and DDS upload bounds.
    for(const auto& field:std::vector<std::pair<size_t,uint32_t>>{
        {0,0}, {4,uint32_t(source.size()+1)}, {8,127}, {8,uint32_t(TextureMaxBytes+1)},
        {44,9}, {headerSize+source.size(),0}, {headerSize+source.size()+16,9}}){
        assert(disk.Write(source,source,desc));setWord(field.first,field.second);readBoth(source,false);
    }
    assert(disk.Write(source,source,desc));flip(12);readBoth(source,false); // header identity
    assert(disk.Write(source,source,desc));
    setWord(52,2);setWord(headerSize+source.size()+28,2);readBoth(source,false); // missing mip bytes
    assert(disk.Write(source,source,desc));
    {std::ofstream f(file,std::ios::binary|std::ios::app);f.put('x');}
    readBoth(source,false); // surplus bytes
    assert(disk.Write(source,source,desc));
    {HANDLE f=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        assert(f!=INVALID_HANDLE_VALUE);LARGE_INTEGER end{};end.QuadPart=headerSize+source.size()*2+31;
        assert(SetFilePointerEx(f,end,nullptr,FILE_BEGIN)&&SetEndOfFile(f));CloseHandle(f);}
    readBoth(source,false); // missing final digest byte, even when digest is skipped
    {std::ofstream f(file,std::ios::binary|std::ios::trunc);f.put('x');}
    readBoth(source,false); // truncated header
    assert(disk.Write(source,source,desc));assert(disk.Read(source,out,upload));assert(out==source);
    {std::ofstream f(file+L".part");f<<"interrupted";}
    assert(disk.Read(source,out,upload));assert(disk.Write(source,source,desc));
    auto invalid=desc;invalid.Width=9;assert(!disk.Write(source,source,invalid));assert(disk.Read(source,out,upload));
    // Exercise the seek and strict chunked comparison beyond the 64 KiB buffer.
    auto large=source;large.resize(65536+160,0x5a);assert(disk.Write(large,source,desc));
    readBoth(large,true);assert(out==source);file=disk.Path(large);flip(headerSize+65536);
    assert(!disk.Read(large,out,upload,true));assert(disk.Read(large,out,upload,false));assert(out==source);
    std::cout<<"Texture disk guards passed: strict/default and trusted reads, skipped source/digest, content-key isolation, namespace, size/layout rejection, large-source seek, repair, atomic publication\n";
}
