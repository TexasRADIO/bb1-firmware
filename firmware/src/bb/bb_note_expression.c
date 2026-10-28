/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_note_expression.h"
int bb_note_expression_make(unsigned hv,unsigned velocity,unsigned hg,unsigned gate,bb_note_expression *out) {
 if(!out||hv>1||hg>1||velocity>127||gate>100||(!hv&&velocity)||(!hg&&gate))return 0;
 bb_note_expression word=(bb_note_expression)(velocity|(hv<<7)|(gate<<8)|(hg<<15));
 *out=word;return 1;
}
int bb_note_expression_read(bb_note_expression word,bb_note_expression_view *out) {
 if(!out)return 0;
 unsigned velocity=word&127u,hv=(word>>7)&1u,gate=(word>>8)&127u,hg=word>>15;
 if(gate>100||(!hv&&velocity)||(!hg&&gate))return 0;
 bb_note_expression_view view={(uint8_t)hv,(uint8_t)velocity,(uint8_t)hg,(uint8_t)gate};
 *out=view;return 1;
}
#include "gate-percent-bits.inc"
int bb_note_expression_gate_bits_match(unsigned percent,const uint8_t *raw) {
 if(!raw||percent>100)return 0;
 for(unsigned i=0;i<8;i++)if(raw[i]!=bb_gate_percent_bits[percent][i])return 0;
 return 1;
}
