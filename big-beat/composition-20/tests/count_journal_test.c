/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_pattern_edit.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CK(x) do{checks++;assert(x);}while(0)
int main(void){
 bb_pattern_edit e={0},before;bb_phrase phrase={0},out;unsigned machine=9;
 for(unsigned i=0;i<12;i++)CK(bb_pe_push_count(&e,i&1u,100+(i&1u),7,i,0,1,2+i%3)==BB_PE_OK);
 CK(e.count==8&&e.history[0].voice==4&&e.history[7].voice==11);
 before=e;CK(bb_pe_push_count(&e,0,100,7,0,64,1,4)==BB_PE_INVALID);CK(!memcmp(&e,&before,sizeof e));
 CK(bb_pe_push_count(&e,0,100,7,0,0,1,1)==BB_PE_UNCHANGED);CK(!memcmp(&e,&before,sizeof e));
 CK(bb_pe_undo(&e,&machine,&out)==BB_PE_CHANGED&&machine==9);CK(!memcmp(&e,&before,sizeof e));
 phrase.length=16;phrase.count=1;phrase.events[0]=BB_FDV_BD;
 CK(bb_pe_push(&e,0,&phrase)==BB_PE_OK);CK(e.count==8&&e.history[0].voice==5);
 CK(e.history[7].kind==BB_PE_ENTRY_PHRASE&&!e.history[7].source_ticket&&!e.history[7].source_epoch&&!e.history[7].voice);
 CK(bb_pe_undo(&e,&machine,&out)==BB_PE_OK&&machine==0&&!memcmp(&out,&phrase,sizeof out));
 CK(e.count==7&&e.history[6].voice==11);
 bb_pe_drop_newest(&e,1);CK(e.count==6&&e.history[5].voice==10);
 bb_pe_forget(&e,0);CK(e.count==3&&bb_pe_entries(&e,0)==0&&bb_pe_entries(&e,1)==3);
 for(unsigned i=0;i<e.count;i++)CK(e.history[i].kind==BB_PE_ENTRY_NATIVE_COUNT&&e.history[i].voice==5+2*i&&e.history[i].source_ticket==101&&e.history[i].source_epoch==7);
 before=e;bb_pe_filter_native(&e,0,0,0,0);CK(!memcmp(&e,&before,sizeof e));
 bb_pe_filter_native(&e,1,1,101,7);CK(!memcmp(&e,&before,sizeof e));
 bb_pe_filter_native(&e,1,1,101,8);CK(e.count==0);
 CK(bb_pe_push(&e,0,&phrase)==BB_PE_OK);
 CK(bb_pe_push_count(&e,0,100,7,0,0,1,2)==BB_PE_OK);
 CK(bb_pe_push_count(&e,1,101,7,0,0,1,2)==BB_PE_OK);
 bb_pe_forget_phrases(&e,0);CK(e.count==2&&e.history[0].machine==0&&e.history[1].machine==1);
 bb_pe_filter_native(&e,0,1,102,7);CK(e.count==1&&e.history[0].machine==1);
 CK(bb_pe_push(&e,0,&phrase)==BB_PE_OK);bb_pe_filter_native(&e,1,0,0,0);
 CK(e.count==1&&e.history[0].kind==BB_PE_ENTRY_PHRASE&&e.history[0].machine==0);
 e.count=BB_PE_HISTORY+1;before=e;bb_pe_filter_native(&e,0,0,0,0);bb_pe_forget_phrases(&e,0);CK(!memcmp(&e,&before,sizeof e));
 printf("shared count journal: %u checks PASS\n",checks);return 0;
}
