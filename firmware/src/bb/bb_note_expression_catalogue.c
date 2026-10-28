/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_note_expression_catalogue.h"
int bb_note_expression_from_catalogue(const bb_catalogue *c,bb_catalogue_id note,bb_note_expression *out) {
 if(!out||!bb_catalogue_private(c,out,sizeof *out)||bb_catalogue_kind(c,note)!=BB_CAT_OBJECT)return 0;
 bb_catalogue_id v=bb_catalogue_find(c,note,"velocity"),g=bb_catalogue_find(c,note,"gate");
 unsigned hv=v!=BB_CATALOGUE_NONE,hg=g!=BB_CATALOGUE_NONE;uint32_t velocity=0,gate=0;
 if(hv&&!bb_catalogue_unsigned(c,v,127,&velocity))return 0;
 if(hg) {
  uint8_t bits[8];
  if(!bb_catalogue_scaled(c,g,100,1,100,&gate)||!bb_pr_number_bits(c,g,bits)||
     !bb_note_expression_gate_bits_match(gate,bits))return 0;
 }
 bb_note_expression word;
 if(!bb_note_expression_make(hv,velocity,hg,gate,&word))return 0;
 *out=word;return 1;
}
