/* SPDX-License-Identifier: GPL-3.0-only */
#if BB_WITH_EVOLVE && BB_WITH_RICH_DRUM_RENDER
static void bb_gen_draw(void){
 static const char *const voice[]={"KICK","SNARE","LOW TOM","MID TOM","HIGH TOM","LOW CONGA","MID CONGA","HIGH CONGA","RIM","CLAVE","CLAP","MARACAS","COWBELL","CYMBAL","OPEN HAT","CLOSED HAT"};
 const char *const eu[]={"HITS","STEPS","ROTATE","ACCENTS"};
 const char *const ra[]={"DENSITY","ACCENT",bb_face.requested.page==BB_DRUMS?"ANCHOR":"SLIDE","SEED"};
 unsigned value[4];
 if(bb_gen_state.mode==1){value[0]=bb_gen_state.hits;value[1]=bb_gen_state.steps;value[2]=bb_gen_state.rotation;value[3]=bb_gen_state.accents;}
 else{value[0]=bb_gen_state.density;value[1]=bb_gen_state.accent;value[2]=bb_face.requested.page==BB_DRUMS?bb_gen_state.anchor:bb_gen_state.slide;value[3]=bb_gen_state.seed;}
 for(unsigned strip=0;strip<240;strip+=120){
  bb_backdrop(strip,120,0);
  if(!strip){cv_text(9,6,&AF_M,bb_gen_state.mode==1?"EUCLID":"RANDOM",BB_TEXT);
   cv_text(9,32,&AF_S,bb_face.requested.page==BB_DRUMS?voice[bb_gen_state.voice]:"DILLO / AUTHORED HOME KEPT",BB_DIM);
   for(unsigned k=0;k<4;k++){cv_text(9+(int)k*56,58,&AF_S,bb_gen_state.mode==1?eu[k]:ra[k],BB_DIM);bb_number(9+(int)k*56,78,value[k],&AF_M,BB_TEXT);}
  }else{
   cv_text(9,10,&AF_S,"EDIT APPLY / BLACK 2 RETURN",BB_TEXT);
   cv_text(9,35,&AF_S,"BLACK 3 MODE / HOME STUDIO",BB_DIM);
   cv_text(9,60,&AF_S,bb_face.requested.page==BB_DRUMS?"WHITE KEYS: CHOOSE SOURCE VOICE":"ROOT / SCALE / LOCKS ON EVOLVE",BB_DIM);
   cv_text(9,90,&AF_S,bb_hint_until?bb_hint:"VARIATIONS KEEP THE SOURCE",BB_TEXT);
  }
  cv_blit(0,strip);
 }
}
#endif
