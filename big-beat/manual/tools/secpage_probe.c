/* Lane LP-P4: OCT+ on HOME (PERFORM in RC2: pages of 16) through the real bb_nav_input with 54 sections: the notice each page change gives. */
#include <stdio.h>
#include <string.h>
#include "bb_nav.h"
int main(void){
  bb_nav_state s; bb_nav_out o; bb_nav_in in; unsigned t=1000;
  bb_nav_init(&s,BB_NAV_STUDIO,400); bb_nav_set_features(&s,BB_NAV_F_TAP|BB_NAV_F_SCALE|BB_NAV_F_THROW|BB_NAV_F_STEP_PITCH);
  bb_nav_set_instrument(&s,BB_NAV_INSTR_DRUM,2); bb_nav_set_focus(&s,0);
  memset(&in,0,sizeof in); in.facts.section=0; in.facts.section_count=54; in.facts.running=1;
  in.facts.kit_slot=in.facts.kit_slot_queued=in.facts.section_queued=in.facts.list_queued=0xff;
  memset(&o,0,sizeof o); in.now_ms=t; bb_nav_input(&s,&in,&o);
  for(int i=0;i<6;i++){
    memset(&o,0,sizeof o); in.pressed=1u<<BB_NAV_B_OCTUP; in.held=in.pressed; in.now_ms=(t+=50); bb_nav_input(&s,&in,&o);
    printf("OCT+ %d: section page %u notice \"%s\"\n",i+1,bb_nav_section_page(&s)+1,bb_nav_message_text(o.message));
    memset(&o,0,sizeof o); in.pressed=0; in.held=0; in.now_ms=(t+=50); bb_nav_input(&s,&in,&o);
  }
  return 0;}
