#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "pac_archive.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#pragma comment(lib,"bcrypt.lib")
using namespace vii::pac;
namespace fs = std::filesystem;
template<class F> void Reject(F f) { bool failed=false; try { f(); } catch(const std::exception&) { failed=true; } assert(failed); }
std::string NamespaceHash(const std::string& name) {
    BCRYPT_ALG_HANDLE a=nullptr; std::array<unsigned char,32> hash{};
    assert(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    assert(BCryptHash(a,nullptr,0,(PUCHAR)name.data(),ULONG(name.size()),hash.data(),32)>=0);
    BCryptCloseAlgorithmProvider(a,0); const char* hex="0123456789abcdef"; std::string s;
    for(auto c:hash){s+=hex[c>>4];s+=hex[c&15];}return s;
}
void Unit() {
    assert(NormalizePath("/MODEL/main.CL3",true)=="model/main.cl3");
    assert(ArchiveNamespace("SOUND/BGM00001.pac")=="sound/bgm");
    assert(ManagerNamespace("CONTENTS/SYSTEM")=="contents/system");
    assert(ManagerNamespace("DLC\\DLC0000000000121")=="dlc/dlc0000000000121");
    assert(ManagerNamespace("DLC/GAME")!=ManagerNamespace("CONTENTS/GAME"));
    for(auto s:{"../x","/abs","C:/x","a//b","x/../b","con.txt","com1","name.","x/ ","x:ads",""}) Reject([&]{NormalizePath(s);});
    Reject([]{ArchiveNamespace("GAME.pac");}); Reject([]{ManagerNamespace("OTHER/GAME");});
    const unsigned char leaf[]={0x20,0x80};auto repeated=DecodeHuffman(leaf,2,19);
    assert(repeated==std::vector<unsigned char>(19,'A'));Reject([&]{DecodeHuffman(leaf,1,19);});
    std::vector<unsigned> bits;
    auto put=[&](unsigned value,unsigned n){while(n--)bits.push_back((value>>n)&1);};
    put(1,1);put(0,1);put('A',8);put(0,1);put('B',8);put(0,1);put(1,1);put(1,1);put(0,1);
    std::vector<unsigned char> encoded((bits.size()+7)/8);
    for(size_t i=0;i<bits.size();++i)encoded[i/8]|=unsigned char(bits[i]<<(7-i%8));
    auto result=DecodeHuffman(encoded.data(),encoded.size(),4);assert(std::string(result.begin(),result.end())=="ABBA");
    std::istringstream raw("prefixDATA");Entry e{"x",4,4,0,6};std::string out;
    Decode(raw,e,[&](const unsigned char* p,size_t n){out.append((const char*)p,n);});assert(out=="DATA");
    Reject([&]{Decode(raw,e,[](const unsigned char*,size_t){},[]{return true;});});
    e.packed=5;e.unpacked=5;Reject([&]{Decode(raw,e,[](const unsigned char*,size_t){});});
    std::cout<<"PAC paths, Huffman trees/decisions/truncation, raw reads and cancellation pass\n";
}
int wmain(int argc,wchar_t** argv) {
    try {
        Unit(); if(argc==1)return 0;
        if(argc!=3)throw std::runtime_error("Usage: pac-archive-test.exe PAC-folder game-decoded-corpus-folder");
        fs::path root=argv[1], corpus=argv[2];size_t matched=0,archives=0,total=0;uint64_t bytes=0;
        for(auto& item:fs::recursive_directory_iterator(root)) {
            if(!item.is_regular_file()||item.path().extension()!=L".pac")continue;
            auto archive=Inspect(item.path());++archives;total+=archive.entries.size();
            auto group=ArchiveNamespace(fs::relative(item.path(),root));for(auto& c:group)if(c>='a'&&c<='z')c=char(c-'a'+'A');
            auto ns=NamespaceHash("CONTENTS/"+group);std::ifstream input(item.path(),std::ios::binary);
            for(const auto& e:archive.entries) {
                auto expected=corpus/ns/fs::path(e.name);if(!fs::exists(expected))continue;
                if(fs::file_size(expected)!=e.unpacked)throw std::runtime_error("Corpus size mismatch: "+e.name);
                std::ifstream check(expected,std::ios::binary);uint64_t done=0;
                Decode(input,e,[&](const unsigned char* p,size_t n){std::vector<char> value(n);if(!check.read(value.data(),n)||std::memcmp(value.data(),p,n))throw std::runtime_error("Decoded mismatch: "+e.name);done+=n;});
                if(done!=e.unpacked)throw std::runtime_error("Decoded length mismatch");++matched;bytes+=done;
            }
            std::cout<<item.path().filename().string()<<": cumulative matches="<<matched<<"\n"<<std::flush;
        }
        size_t expected=0;for(auto& p:fs::recursive_directory_iterator(corpus))if(p.is_regular_file())++expected;
        if(matched!=expected)throw std::runtime_error("Not every corpus file matched an archive entry");
        std::cout<<"PASS archives="<<archives<<" indexed="<<total<<" byte_verified_files="<<matched<<" bytes="<<bytes<<"\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
