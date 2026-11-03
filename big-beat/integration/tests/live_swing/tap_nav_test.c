/* SPDX-License-Identifier: GPL-3.0-only
 * N12 first step (Lane Q, test only): what REC+GLO tap tempo produces on the real bb_nav.c (BB_NAV_F_TAP).
 *  1. Taps at a steady interval give tempo_x10 = round(600000*n/sum of the last n<=3 intervals) and the action
 *     BB_NAV_A_TEMPO_SET; the firmware sends BB_CMD_TEMPO round(tempo_x10/10) clamped 30..300
 *     (bb_nav_firmware.inc). The first tap of a sequence sets nothing.
 *  2. A gap over 2 s starts a new sequence; a tap within 100 ms of the last is ignored (contact bounce).
 *  3. Tap produces no transport, page, pattern, section or step action: only BB_NAV_A_TAP and TEMPO_SET.
 *  4. Every page: REC+GLO taps (it never opens GLOBAL while REC is held).
 * Build: clang -I firmware/src tap_nav_test.c firmware/src/bb_nav.c */
#include "bb_nav.h"
#include <stdio.h>
#include <string.h>
static uint32_t now_ms=1000;static unsigned long checks,failures;
#define CHECK(c,...) do{checks++;if(!(c)){failures++;if(failures<30){printf("FAIL: ");printf(__VA_ARGS__);printf("\n");}}}while(0)
#define BIT(b) (1u<<(b))
static bb_nav_out scan(bb_nav_state *s,uint32_t pressed,uint32_t held,uint32_t dt){
 bb_nav_in in;bb_nav_out o;memset(&in,0,sizeof in);memset(&o,0,sizeof o);now_ms+=dt;
 in.pressed=pressed;in.held=held;in.now_ms=now_ms;
 in.facts.kit_slot=in.facts.kit_slot_queued=in.facts.section_queued=BB_NAV_NONE_IDX;
 in.facts.list_cursor=in.facts.list_playing=in.facts.list_queued=BB_NAV_NONE_IDX;
 in.facts.section_count=4;in.facts.list_count=16;in.facts.step_banks=2;
 bb_nav_input(s,&in,&o);return o;
}
static void fresh(bb_nav_state *s,uint8_t page){
 bb_nav_out o;bb_nav_init(s,BB_NAV_STUDIO,600);bb_nav_set_instrument(s,BB_NAV_INSTR_DRUM,2);bb_nav_set_focus(s,0);
 bb_nav_set_features(s,BB_NAV_F_TAP|BB_NAV_F_SCALE|BB_NAV_F_THROW|BB_NAV_F_STEP_PITCH);  /* the firmware's BB_NAV_FEATURES */
 memset(&o,0,sizeof o);bb_nav_goto(s,page,&o);
}
static unsigned firmware_bpm(unsigned x10){int v=((int)x10+5)/10;return (unsigned)(v<30?30:v>300?300:v);}
/* Tap n times at interval iv ms (REC held, GLO pressed); returns the last out and the number of TEMPO_SETs. */
static bb_nav_out taps(bb_nav_state *s,unsigned n,uint32_t iv,unsigned *sets,uint32_t *other_actions){
 const uint32_t REC=BIT(BB_NAV_B_REC),GLO=BIT(BB_NAV_B_GLO);bb_nav_out o,last;memset(&last,0,sizeof last);
 o=scan(s,REC,REC,10);*other_actions|=o.actions;
 for(unsigned i=0;i<n;i++){
  o=scan(s,GLO,REC|GLO,i?iv-20u:10u);*other_actions|=o.actions&~(BB_NAV_A_TAP|BB_NAV_A_TEMPO_SET);
  if(o.actions&BB_NAV_A_TEMPO_SET){(*sets)++;last=o;}
  o=scan(s,0,REC,20);*other_actions|=o.actions&~(BB_NAV_A_TAP|BB_NAV_A_TEMPO_SET);
 }
 scan(s,0,0,10);return last;
}
int main(void){
 unsigned long cases=0,bpm_cases=0;
 static const uint32_t ivs[]={2000,1999,1000,600,500,476,400,300,250,200,150,120,101};
 for(uint8_t page=0;page<BB_NAV_PAGES;page++)for(unsigned i=0;i<sizeof ivs/sizeof ivs[0];i++){
  bb_nav_state s;fresh(&s,page);unsigned sets=0;uint32_t other=0;uint8_t before=s.page;
  bb_nav_out o=taps(&s,5,ivs[i],&sets,&other);
  unsigned want_x10=(unsigned)((600000u*3u+(3u*ivs[i])/2u)/(3u*ivs[i]));if(want_x10<300)want_x10=300;if(want_x10>3000)want_x10=3000;
  CHECK(sets==4,"page %u iv %u: %u tempo sets from 5 taps",page,ivs[i],sets);
  CHECK(o.tempo_x10==want_x10,"page %u iv %u: tempo_x10 %u want %u",page,ivs[i],o.tempo_x10,want_x10);
  CHECK(!other,"page %u iv %u: tapping emitted other actions 0x%x",page,ivs[i],other);
  CHECK(s.page==before,"page %u: tapping changed the page to %u",page,s.page);
  if(page==0)printf("  tap every %4u ms -> tempo_x10 %4u -> BB_CMD_TEMPO %3u BPM\n",ivs[i],o.tempo_x10,firmware_bpm(o.tempo_x10));
  cases++;bpm_cases+=sets;
 }
 /* bounce and timeout */
 {bb_nav_state s;fresh(&s,0);unsigned sets=0;uint32_t other=0;taps(&s,1,500,&sets,&other);
  CHECK(sets==0,"one tap sets nothing");
  sets=0;bb_nav_out o=taps(&s,4,2100,&sets,&other);CHECK(sets==0,"taps 2.1 s apart never set a tempo (%u)",sets);(void)o;
  /* 90 ms apart: the 2nd and 4th taps are bounce; the 3rd is 180 ms after the 1st and sets 333 -> 300 BPM */
  fresh(&s,0);sets=0;o=taps(&s,4,90,&sets,&other);CHECK(sets==1&&o.tempo_x10==3000,"taps under 100 ms are bounce (%u sets)",sets);}
 /* what a tap sequence emits besides the tempo: nothing that touches transport, patterns or steps */
 {bb_nav_state s;fresh(&s,0);unsigned sets=0;uint32_t other=0;taps(&s,6,500,&sets,&other);
  CHECK(!other,"tap emitted other actions 0x%x",other);}
 printf("%s tap nav: %lu tap sequences on %u pages, %lu tempo sets, %lu checks, %lu failures\n",failures?"FAIL":"PASS",cases,BB_NAV_PAGES,bpm_cases,checks,failures);
 return failures?1:0;
}
