#include "texture_sources.hpp"
#include "texture_pipeline.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cassert>
#include <cstring>
namespace fs=std::filesystem;
using namespace vii;
std::vector<unsigned char> Fixture(bool be,bool embedded){
    std::vector<unsigned char> b(0x80+40,0);b[0]='T';b[1]='I';b[2]='D';b[3]=be?0x81:0x90;
    auto w=[&](size_t o,uint32_t v){if(be)v=_byteswap_ulong(v);memcpy(b.data()+o,&v,4);};
    auto h=[&](size_t o,uint16_t v){if(be)v=_byteswap_ushort(v);memcpy(b.data()+o,&v,2);};
    w(4,be?0x80:unsigned(b.size()));w(8,0x80);w(12,1);w(20,32);w(0x44,8);w(0x48,8);h(0x52,2);w(0x58,40);w(0x5c,0x80);w(0x64,0x31545844);
    for(size_t i=128;i<b.size();++i)b[i]=static_cast<unsigned char>(i);
    if(embedded)b.insert(b.begin(),37,0);return b;
}
int wmain(int argc,wchar_t** argv){
    std::vector<unsigned char> expected;
    for(bool be:{false,true})for(bool embedded:{false,true}){
        auto b=Fixture(be,embedded);unsigned count=0;
        ScanTextureSources(b,[&](TextureSource&& s){++count;assert(s.issue.empty());assert(s.offset==size_t(embedded?37:0));assert(s.dds.size()==169);assert(s.dds[28]==2&&s.dds[24]==0&&s.dds.back()==0);
            if(expected.empty())expected=s.dds;else assert(expected==s.dds);});assert(count==1);
        b.pop_back();count=0;ScanTextureSources(b,[&](TextureSource&& s){++count;assert(!s.issue.empty());});
    }
    for(bool rotated:{false,true}){
        auto b=Fixture(false,false);b.resize(128+256);b[3]=rotated?0x92:0x90;
        auto put=[&](size_t at,uint32_t n){memcpy(b.data()+at,&n,4);};put(4,unsigned(b.size()));put(0x4c,32);put(0x50,0x10001);put(0x58,256);put(0x60,0);
        for(size_t i=128;i<b.size();++i)b[i]=static_cast<unsigned char>(i);unsigned count=0;
        ScanTextureSources(b,[&](TextureSource&& s){++count;assert(s.issue.empty()&&s.singleMip&&s.dds.size()==385);
            assert(s.dds[80]==0x41&&s.dds[28]==0&&s.dds[21]==1);assert(s.dds[128]==b[rotated?129:128]);assert(s.dds[131]==b[rotated?128:131]);});assert(count==1);
    }
    puts("TID endian/embedded/mip/trailer/bounds fixtures passed");
    if(argc!=3)return argc==1?0:2;
    std::ofstream out(argv[2],std::ios::binary);size_t files=0,sources=0,issues=0;
    for(auto& f:fs::recursive_directory_iterator(argv[1]))if(f.is_regular_file()){
        auto size=f.file_size();if(!size||size>512u*1024*1024)continue;
        std::ifstream stream(f.path(),std::ios::binary);std::vector<unsigned char> bytes(size_t(size),0);stream.read(reinterpret_cast<char*>(bytes.data()),size);
        ScanTextureSources(bytes,[&](TextureSource&& s){auto h=s.issue.empty()?TextureDigestText(Hash(s.dds.data(),s.dds.size())):"";
            out<<h<<'\t'<<fs::relative(f.path(),argv[1]).generic_u8string()<<'\t'<<s.offset<<'\t'<<s.image<<'\t'<<s.issue<<'\n';++sources;if(!s.issue.empty())++issues;});
        if(++files%1000==0){printf("files=%zu sources=%zu issues=%zu\n",files,sources,issues);fflush(stdout);}
    }
    printf("complete files=%zu sources=%zu issues=%zu\n",files,sources,issues);return 0;
}
