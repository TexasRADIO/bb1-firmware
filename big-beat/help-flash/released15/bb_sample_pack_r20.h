/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat cold sample-pack directory candidate. Read-only; NOT a flash writer.
 * One exclusive owner of the former asset region. Not compatible with 909 pack
 * or stock FSMP slots; migration and update protocol are separate requirements.
 */
#ifndef BB_SAMPLE_PACK_R20_H
#define BB_SAMPLE_PACK_R20_H
#include "bb_sample_fsmp_r18.h"
#include "bb_sample_directory.h"
/* Header 256 bytes: BBSK, version u16, count u8, zero; total u32,
 * payload CRC u32, header CRC u32 (excluding itself), 12 zero bytes.
 * Eight 28-byte entries: offset, bytes, CRC, 12-byte nul-terminated name,
 * four zero bytes. Each entry contains 16 note IDs, 16 choke IDs, exact FSMP.
 */
static int bb_sample_pack_valid(const uint8_t*p,size_t bytes){
 if(!p||bytes<256||!bb_sample_directory_valid(p,bytes)||
    ~bb_crc32_add(~0u,p+256,bytes-256)!=bb_fsmp_u32(p+12))return 0;
 for(unsigned i=0;i<p[6];i++){
  const uint8_t*e=p+32+i*28;
  uint32_t off=bb_fsmp_u32(e),n=bb_fsmp_u32(e+4);
  if(~bb_crc32_add(~0u,p+off,n)!=bb_fsmp_u32(e+8))return 0;
  bb_sample_zone zones[16];unsigned nz;
  if(!bb_sample_fsmp_parse(p+off+32,n-32,p+off,p+off+16,p[off+38],zones,&nz))return 0;
 }
 return 1;
}
/* Cold-only extraction. Revalidates pack before publishing any selected bank.
 * Directory remains immutable for every live sample voice. */
static int bb_sample_pack_bank(const uint8_t*p,size_t bytes,unsigned bank,
                               bb_sample_zone*out,unsigned*out_count){
 if(!bb_sample_pack_valid(p,bytes)||bank>=p[6]||!out||!out_count)return 0;
 unsigned count=p[bb_fsmp_u32(p+32+bank*28)+38];
 if(bb_fsmp_overlap(out,count*sizeof *out,p,bytes)||
    bb_fsmp_overlap(out_count,sizeof *out_count,p,bytes))return 0;
 const uint8_t*e=p+32+bank*28;uint32_t off=bb_fsmp_u32(e),n=bb_fsmp_u32(e+4);
 return bb_sample_fsmp_parse(p+off+32,n-32,p+off,p+off+16,count,out,out_count);
}
#endif
