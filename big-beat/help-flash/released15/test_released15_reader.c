/* SPDX-License-Identifier: GPL-3.0-only
 * The released BB1 1.5 sample-library reader (bb1-firmware v1.5 e3d41c5, the 12 reader files pinned by Git blob in
 * PINS.json) on the 2.0 library with the help block (bb1-sounds-1.4.bin) against bb1-sounds-1.3.bin: 1.5 accepts both
 * and reads every kit and zone byte-identically; the files differ only inside the text area; the text block validates
 * for the 2.0 build and the 1.3 library's text area is refused (fallback). Host only, ASan/UBSan.
 * usage: test_released15_reader OLD.bin NEW.bin   (compile: see big-beat/help-flash/released15/run.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb_sample_pack_r20.h"
#include "bb/bb_text_block.h"
static unsigned checks;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char *load(const char *p,size_t *n){FILE *f=fopen(p,"rb");CK(f);CK(!fseek(f,0,SEEK_END));long z=ftell(f);CK(z>0);
 *n=(size_t)z;rewind(f);unsigned char *b=malloc(*n);CK(b);CK(fread(b,1,*n,f)==*n);CK(!fclose(f));return b;}
int main(int argc,char **argv){
 CK(argc==3);size_t an,bn;unsigned char *a=load(argv[1],&an),*b=load(argv[2],&bn);
 const unsigned rel=BB_TX_FLASH_OFF-0xA0000u;
 CK(an==245760&&bn==an);
 unsigned declared=bb_fsmp_u32(a+8);CK(bb_fsmp_u32(b+8)==declared&&declared<=rel);
 for(size_t i=0;i<an;i++)if(i<rel||i>=rel+BB_TX_FLASH_MAX)CK(a[i]==b[i]);      /* byte-diff confined to the text area */
 /* released 1.5 cold-loader calls: the declared BBSK length from its header, never the padded file size */
 CK(bb_sample_pack_valid(a,declared));CK(bb_sample_pack_valid(b,declared));CK(a[6]==b[6]);
 unsigned zones=0;
 for(unsigned k=0;k<a[6];k++){
  bb_sample_zone x[16],y[16];unsigned xn=0,yn=0;memset(x,0,sizeof x);memset(y,0,sizeof y);
  CK(bb_sample_pack_bank(a,declared,k,x,&xn));CK(bb_sample_pack_bank(b,declared,k,y,&yn));CK(xn==yn&&xn>0);
  for(unsigned j=0;j<xn;j++){
   CK(x[j].frames==y[j].frames&&x[j].rate==y[j].rate&&x[j].note==y[j].note&&x[j].choke==y[j].choke&&x[j].bytes==y[j].bytes);
   CK(x[j].data-a==y[j].data-b);CK((size_t)(x[j].data-a)+x[j].bytes<=declared);CK(!memcmp(x[j].data,y[j].data,x[j].bytes));zones++;
  }
 }
 /* the 2.0 reader: the new image's text area is this build's block; the old one's is refused */
 CK(bb_tx_block_valid(b+rel,BB_TX_FLASH_MAX));CK(!bb_tx_block_valid(a+rel,BB_TX_FLASH_MAX));
 /* the instrument has teeth: BBSK corruption is refused by the 1.5 reader */
 b[declared-1]^=1;CK(!bb_sample_pack_valid(b,declared));b[declared-1]^=1;
 printf("PASS %u checks; released 1.5 reader accepts 1.3 and 1.4; %u entries / %u zones byte-identical; "
        "diff confined to A+0x%x..0x%x; BBTX valid in 1.4, refused in 1.3\n",checks,a[6],zones,rel,rel+BB_TX_FLASH_MAX);
 free(a);free(b);return 0;}
