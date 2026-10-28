/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_crc32.h"
uint32_t bb_crc32_add(uint32_t c,const uint8_t *p,size_t n){
    while(n--){c^=*p++;for(unsigned bit=0;bit<8;bit++)c=(c>>1)^((0u-(c&1u))&UINT32_C(0xedb88320));}
    return c;
}
