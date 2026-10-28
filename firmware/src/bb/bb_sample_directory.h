/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_DIRECTORY_H
#define BB_SAMPLE_DIRECTORY_H
#include "bb_wire.h"
#include "bb_crc32.h"
#define BB_SAMPLE_PACK_LIMIT 245760u
/* Header-only validation. p must reference 256 readable bytes; bytes is the
 * declared total pack length. This does NOT validate audio payload content. */
static uint32_t bb_sample_pack_header_crc(const uint8_t *p){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,16),p+20,236);
}
static int bb_sample_directory_valid(const uint8_t *p,size_t bytes){
    if(!p||bytes<800||bytes>BB_SAMPLE_PACK_LIMIT||bb_wire_get32(p)!=0x4b534242u||
       p[4]!=1||p[5]||!p[6]||p[6]>8||p[7]||bb_wire_get32(p+8)!=bytes||
       bb_sample_pack_header_crc(p)!=bb_wire_get32(p+16))return 0;
    for(unsigned i=20;i<32;i++)if(p[i])return 0;
    uint32_t end=256;
    for(unsigned i=0;i<8;i++){
        const uint8_t *e=p+32+i*28;
        if(i>=p[6]){for(unsigned j=0;j<28;j++)if(e[j])return 0;continue;}
        uint32_t off=bb_wire_get32(e),n=bb_wire_get32(e+4);
        if(off!=end||off>bytes||n<544||n>81952||n>bytes-off||!e[12])return 0;
        unsigned nul=0;
        for(unsigned j=12;j<24;j++){if(!e[j])nul=1;else if(nul||e[j]<32||e[j]>126)return 0;}
        if(!nul||e[24]||e[25]||e[26]||e[27])return 0;
        end=off+n;
    }
    return end==bytes;
}
#endif
