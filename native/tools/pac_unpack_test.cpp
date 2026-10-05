#include "pac_unpack.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace vii;
template<class F> void Reject(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}
void U32(std::vector<unsigned char>& b,size_t offset,uint32_t value){for(unsigned i=0;i<4;++i)b[offset+i]=static_cast<unsigned char>(value>>(8*i));}
void Archive(const fs::path& p,const char* first="folder/a.bin"){
    std::vector<unsigned char> b(20+576+35);std::memcpy(b.data(),"DW_PACK\0",8);U32(b,12,2);
    strcpy_s(reinterpret_cast<char*>(b.data()+28),260,first);U32(b,20+272,5);U32(b,20+276,5);
    strcpy_s(reinterpret_cast<char*>(b.data()+316),260,"folder/repeated.bin");U32(b,308+272,30);U32(b,308+276,16);U32(b,308+280,1);U32(b,308+284,5);
    std::memcpy(b.data()+596,"hello",5);U32(b,601,0x1234);U32(b,605,1);U32(b,609,131072);U32(b,613,28);U32(b,617,16);U32(b,621,2);b[629]=0x20;b[630]=0x80;
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<char*>(b.data()),b.size());
}
int wmain(int argc,wchar_t** argv){
    try {
        if(argc!=2)throw std::runtime_error("Supply a fresh test folder");fs::path root=argv[1];assert(!fs::exists(root));fs::create_directories(root/L"source");
        auto source=root/L"source";Archive(source/L"GAME00000.pac");auto output=root/L"output";
        bool cancel=false;Reject([&]{unpack::Run(source,output,[&](const unpack::Progress&){cancel=true;},[&]{return cancel;});});
        assert(!fs::exists(output/L"manifest.vii"));for(auto& p:fs::recursive_directory_iterator(output))assert(p.path().wstring().find(L"vii-part-")==std::wstring::npos);
        auto result=unpack::Run(source,output);assert(result.files==2&&result.bytes==21&&result.reused==0);
        auto m=loose::ReadManifest(output/L"manifest.vii");assert(m.files.size()==2&&m.sources.size()==1&&m.sources[0].group=="game");
        assert(loose::Matches(source/L"GAME00000.pac",m.sources[0]));assert(unpack::Verify(output).files==2);
        auto again=unpack::Run(source,output);assert(again.reused==2);
        {std::ofstream f(output/L"game/folder/a.bin",std::ios::binary);f<<"WRONG";}
        Reject([&]{unpack::Verify(output);});Reject([&]{unpack::Run(source,output);});
        {std::ifstream f(output/L"game/folder/a.bin");std::string s;f>>s;assert(s=="WRONG");}
        fs::create_directories(root/L"unowned");{std::ofstream f(root/L"unowned/user.txt");f<<"preserve";}Reject([&]{unpack::Run(source,root/L"unowned");});
        auto bad=root/L"bad";fs::create_directories(bad);Archive(bad/L"GAME00000.pac","../escape");Reject([&]{unpack::Run(bad,root/L"bad-output");});assert(!fs::exists(root/L"escape"));
        {std::fstream f(output/L"manifest.vii",std::ios::binary|std::ios::in|std::ios::out);f.seekp(25);f.put('X');}Reject([&]{loose::ReadManifest(output/L"manifest.vii");});
        {std::ofstream f(source/L"GAME00000.pac",std::ios::binary|std::ios::app);f.put('x');}assert(!loose::Matches(source/L"GAME00000.pac",m.sources[0]));
        // Game/CONTENTS selections discover sibling DLC without changing the
        // original manifest layout or colliding with equal base group names.
        auto game=root/L"game",assets=root/L"combined-output";
        fs::create_directories(game/L"CONTENTS");Archive(game/L"CONTENTS/GAME00000.pac");
        auto baseOnly=unpack::Run(game,assets);assert(baseOnly.files==2&&unpack::FindDlc(game).empty());
        assert(!fs::exists(loose::DlcOutput(assets)));
        fs::create_directories(game/L"DLC");assert(unpack::Run(game,assets).reused==2);
        Archive(game/L"DLC/GAME00000.pac");
        bool dlcCancel=false;Reject([&]{unpack::Run(game,assets,[&](const unpack::Progress& p){
            if(p.current.rfind(L"DLC: ",0)==0)dlcCancel=true;},[&]{return dlcCancel;});});
        assert(fs::exists(assets/L"manifest.vii")&&!fs::exists(loose::DlcOutput(assets)/L"manifest.vii"));
        auto combined=unpack::Run(game/L"contents",assets);assert(combined.files==4&&combined.reused==2);
        assert(fs::exists(assets/L"game/folder/a.bin")&&fs::exists(loose::DlcOutput(assets)/L"game/folder/a.bin"));
        assert(unpack::Verify(assets).files==4&&unpack::Run(game,assets).reused==4);
        fs::rename(game/L"DLC",game/L"DLC-disabled");
        assert(unpack::FindDlc(game).empty()&&unpack::Run(game,assets).files==2);
        fs::rename(game/L"DLC-disabled",game/L"DLC");assert(unpack::Run(game,assets).reused==4);
        // Directly selecting a DLC/PAC folder remains a standalone extraction.
        assert(unpack::Run(game/L"DLC",root/L"dlc-only").files==2);
        std::cout<<"PASS mixed compression, cancellation cleanup, resume/reuse, full hashes, corrupt manifest/output, source mismatch, traversal and unrelated-file preservation\n";
        std::cout<<"PASS optional DLC discovery, namespace separation, DLC cancellation/resume, removal/reappearance and standalone PAC-folder compatibility\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
