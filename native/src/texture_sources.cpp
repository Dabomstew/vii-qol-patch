#include "texture_sources.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace vii {
namespace {
uint32_t U32(const unsigned char* p,bool be){uint32_t v;memcpy(&v,p,4);return be?_byteswap_ulong(v):v;}
uint16_t U16(const unsigned char* p,bool be){uint16_t v;memcpy(&v,p,2);return be?_byteswap_ushort(v):v;}
void Put(std::vector<unsigned char>& d,size_t o,uint32_t v){memcpy(d.data()+o,&v,4);}
void Issue(const std::function<void(TextureSource&&)>& emit,size_t off,unsigned image,const char* why){TextureSource s;s.offset=off;s.image=image;s.issue=why;emit(std::move(s));}
// The +4 field is only header length in observed big-endian TID81 files.
// Determine the span from the validated table and every stored payload instead.
size_t Parse(TextureBytes in,size_t base,const std::function<void(TextureSource&&)>& emit){
    const auto* p=in.data()+base;size_t remaining=in.size()-base;if(remaining<0x20)return 0;
    bool be=(p[3]&1)!=0,wide=(p[3]&0x80)!=0;size_t stride=wide?0x60:0x40;
    uint32_t declared=U32(p+4,be),count=std::max(1u,U32(p+12,be)),table=wide?U32(p+20,be):0x10;
    size_t payloadStart=U32(p+8,be);
    if(count>4096||table<0x10||table>remaining||size_t(count)>((remaining-table)/stride))return 0;
    size_t tableEnd=table+size_t(count)*stride,span=tableEnd;
    if(declared<tableEnd||declared>remaining||payloadStart<tableEnd||payloadStart>remaining)return 0;
    for(unsigned i=0;i<count;++i){const auto* r=p+table+size_t(i)*stride;size_t off=U32(r+0x3c,be),n=U32(r+0x38,be);
        if(off<tableEnd||off>remaining||n>remaining-off)return 0;span=std::max(span,off+n);}
    span=std::max(span,size_t(declared));
    for(unsigned image=0;image<count;++image){
        const auto* r=p+table+size_t(image)*stride;uint32_t w=U32(r+0x24,be),h=U32(r+0x28,be),bits=U32(r+0x2c,be);
        unsigned mips=std::max(1u,unsigned(U16(r+0x32,be)));size_t stored=U32(r+0x38,be);uint32_t fourcc=wide?U32(r+0x44,be):0;
        if(!w||!h||w>16384||h>16384||mips>15){Issue(emit,base,image,"dimensions_or_mips");continue;}
        if(bits==32){
            if(p[3]&4){Issue(emit,base,image,"swizzled_raw32");continue;}
            const size_t bytes=size_t(w)*h*4,at=U32(r+0x3c,be);
            if(bytes>stored||bytes>span-at){Issue(emit,base,image,"raw32_payload_size");continue;}
            TextureSource out;out.offset=base;out.image=image;out.singleMip=true;out.dds.assign(bytes+129,0);
            Put(out.dds,0,0x20534444);Put(out.dds,4,124);Put(out.dds,8,0xa1007);Put(out.dds,12,h);Put(out.dds,16,w);Put(out.dds,20,uint32_t(bytes));
            Put(out.dds,76,32);Put(out.dds,80,0x41);Put(out.dds,88,32);Put(out.dds,92,0xff);Put(out.dds,96,0xff00);Put(out.dds,100,0xff0000);Put(out.dds,104,0xff000000);Put(out.dds,108,0x401008);
            memcpy(out.dds.data()+128,p+at,bytes);
            // Format0 TIDs queue VA0x78B110's ARGB -> RGBA byte rotation.
            // An explicit RGB/alpha pixel-format flag selects format2 instead.
            const bool explicitRgb=wide&&U32(p+0x10,be)>0&&(U32(r+0x40,be)&0x41);
            if((p[3]&2)&&!explicitRgb)for(size_t pos=128;pos<128+bytes;pos+=4){auto a=out.dds[pos];out.dds[pos]=out.dds[pos+1];out.dds[pos+1]=out.dds[pos+2];out.dds[pos+2]=out.dds[pos+3];out.dds[pos+3]=a;}
            emit(std::move(out));continue;
        }
        if(count!=1){Issue(emit,base,image,"multi_image_compressed_layout");continue;}
        unsigned block=fourcc==0x31545844?8:(fourcc==0x33545844||fourcc==0x35545844)?16:0;
        if(!block){Issue(emit,base,image,"unsupported_format");continue;}
        size_t total=0;unsigned mw=w,mh=h;
        for(unsigned mip=0;mip<mips;++mip){total+=size_t((mw+3)/4)*((mh+3)/4)*block;mw=std::max(1u,mw/2);mh=std::max(1u,mh/2);}
        if(total>stored||total>span-payloadStart){Issue(emit,base,image,"mip_payload_size");continue;}
        TextureSource out;out.offset=base;out.image=image;out.dds.assign(129+total,0);
        Put(out.dds,0,0x20534444);Put(out.dds,4,124);Put(out.dds,8,0xa1007);Put(out.dds,12,h);Put(out.dds,16,w);Put(out.dds,28,mips);
        Put(out.dds,76,32);Put(out.dds,80,0x45);Put(out.dds,84,fourcc);Put(out.dds,88,32);Put(out.dds,92,0xff);Put(out.dds,96,0xff00);Put(out.dds,100,0xff0000);Put(out.dds,104,0xff000000);Put(out.dds,108,0x401008);
        memcpy(out.dds.data()+128,p+payloadStart,total);emit(std::move(out));
    }
    return span;
}
}
void ScanTextureSources(TextureBytes bytes,const std::function<void(TextureSource&&)>& emit){
    if(!emit||!bytes.data())return;
    for(size_t i=0;i+4<=bytes.size();){
        if(bytes.data()[i]=='T'&&bytes.data()[i+1]=='I'&&bytes.data()[i+2]=='D'){
            auto n=Parse(bytes,i,emit);if(n){i+=n;continue;}
            if(i==0)Issue(emit,0,0,"invalid_tid_bounds");
        }
        ++i;
    }
}
}
