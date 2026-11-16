/* SPDX-License-Identifier: GPL-3.0-only */
#include "sloop_bank.h"
static unsigned u16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t step(uint32_t c,uint8_t b){c^=b;for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);return c;}
const uint8_t *sl_bank_sound(const sl_bank *v,unsigned kit,unsigned lane){
 if(!v||!v->bytes||kit>=31||lane>=16)return 0;
 unsigned i=u16(v->bytes+64+2*(kit*16+lane));
 return i<v->records?v->bytes+1088+22*i:0;
}
int sl_bank_crush(const sl_bank *v,unsigned kit,unsigned *out){
 if(!v||!v->bytes||kit>=31||!out)return 0;
 *out=v->bytes[1056+kit];return 1;
}
int sl_bank_bind(sl_bank *out,const void *image,size_t n){
 if(!out||!image||n!=SL_BANK_BYTES)return 0;
 const uint8_t *p=image;
 if(p[0]!='B'||p[1]!='S'||p[2]!='3'||p[3]!='1'||u16(p+4)!=1||u16(p+6)!=64||u32(p+8)!=n||u16(p+12)!=31||u16(p+14)!=16||u16(p+18)!=22||u32(p+24)!=SL_BANK_RECIPE_CRC||u32(p+28)!=64||u32(p+32)!=1056||u32(p+36)!=1088)return 0;
 unsigned records=u16(p+16);
 if(!records||records>(SL_BANK_BYTES-1088)/22)return 0;
 for(unsigned i=40;i<64;i++)if(p[i])return 0;
 if(p[1087]!=255)return 0;
 for(unsigned i=1088+records*22;i<n;i++)if(p[i]!=255)return 0;
 uint32_t crc=0xffffffffu;
 for(unsigned i=0;i<n;i++)crc=step(crc,i>=20&&i<24?0:p[i]);
 if((crc^0xffffffffu)!=u32(p+20))return 0;
 sl_bank candidate={p,(uint16_t)records};crc=0xffffffffu;
 for(unsigned k=0;k<31;k++){
  crc=step(crc,p[1056+k]);
  for(unsigned l=0;l<16;l++){
   const uint8_t *s=sl_bank_sound(&candidate,k,l);if(!s)return 0;
   for(unsigned j=0;j<22;j++)crc=step(crc,s[j]);
  }
 }
 if((crc^0xffffffffu)!=SL_BANK_RECIPE_CRC)return 0;
 *out=candidate;return 1;
}
