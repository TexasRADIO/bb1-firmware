/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_factory_recipe.h"
#include <string.h>
static int distinct(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int key(const bb_catalogue *c,bb_catalogue_id id,const char *s) {
    const uint8_t *p;size_t n,w=0;while(s[w])w++;
    return bb_catalogue_text(c,id,&p,&n) && n==w && !memcmp(p,s,w);
}
static int number(const bb_catalogue *c,bb_catalogue_id object,const char *name,
                  unsigned scale,unsigned denominator,unsigned minimum,unsigned maximum,unsigned *out,int optional) {
    bb_catalogue_id id=bb_catalogue_find(c,object,name);uint32_t value;
    if(id==BB_CATALOGUE_NONE)return optional;
    if(!bb_catalogue_scaled(c,id,scale,denominator,maximum,&value) || value<minimum)return 0;
    *out=value;return 1;
}
static int boolean(const bb_catalogue *c,bb_catalogue_id object,const char *name,unsigned *out,int optional) {
    bb_catalogue_id id=bb_catalogue_find(c,object,name);
    if(id==BB_CATALOGUE_NONE)return optional;
    int kind=bb_catalogue_kind(c,id);if(kind!=BB_CAT_FALSE && kind!=BB_CAT_TRUE)return 0;
    *out=(unsigned)(kind==BB_CAT_TRUE);return 1;
}
static int fields(const bb_catalogue *c,bb_catalogue_id object,const char *const *names,unsigned count) {
    if(bb_catalogue_kind(c,object)!=BB_CAT_OBJECT)return 0;
    for(unsigned i=0;i<bb_catalogue_count(c,object);i++) {
        bb_catalogue_id id=bb_catalogue_key_at(c,object,i);unsigned j;
        for(j=0;j<count;j++)if(key(c,id,names[j]))break;
        if(j==count)return 0;
    }
    return 1;
}
int bb_factory_read_recipe(const bb_catalogue *c,const bb_factory_choice *choice,bb_factory_recipe *out) {
    if(!c || !choice || !out || !distinct(c,sizeof *c,out,sizeof *out) ||
       !distinct(choice,sizeof *choice,out,sizeof *out) || !bb_catalogue_private(c,out,sizeof *out) ||
       !bb_catalogue_private(c,choice,sizeof *choice) ||
       !bb_factory_choice_valid(c,choice) || choice->group!=BB_BROWSER_ERAS)return BB_RECIPE_INVALID;
    static const char *const dkeys[]={"wave","cutoff","resonance","env","decay","accentAmount","acidDrive","acidAttack","acidLevel","bpm","swing"};
    static const char *const tkeys[]={"square","saw","sub","leadCutoff","leadRes","leadEnv","lfoRate","mod","portamento","transpose","arp","leadAdvanced","leadLevel","bpm","swing"};
    bb_catalogue_id d=bb_catalogue_find(c,bb_catalogue_find(c,choice->bank,"dillo"),"settings");
    bb_catalogue_id t=bb_catalogue_find(c,bb_catalogue_find(c,choice->bank,"telegram"),"settings");
    if(!fields(c,d,dkeys,sizeof dkeys/sizeof *dkeys) || !fields(c,t,tkeys,sizeof tkeys/sizeof *tkeys))return BB_RECIPE_FIELD;
    bb_factory_recipe r;memset(&r,0,sizeof r);unsigned v,tempo,swing;
    if(!bb_catalogue_unsigned(c,bb_catalogue_find(c,choice->bank,"bpm"),300,&v) || v<30)return BB_RECIPE_RANGE;
    r.bpm=(uint16_t)v;
    if(!bb_catalogue_unsigned(c,bb_catalogue_find(c,choice->bank,"swing"),60,&v))return BB_RECIPE_RANGE;
    r.swing_x2=(uint16_t)(100u+v);if(v&1u)r.capabilities|=BB_RECIPE_HALF_SWING;
    if(r.swing_x2>150u)r.capabilities|=BB_RECIPE_SWING_RANGE;
    /* Nested tempo is overridden by the bank in Studio, but still typed. */
    for(unsigned i=0;i<2;i++) {
        bb_catalogue_id object=i?t:d;tempo=r.bpm;swing=r.swing_x2-100u;
        if(!number(c,object,"bpm",1,1,30,300,&tempo,1) || !number(c,object,"swing",1,1,0,60,&swing,1))return BB_RECIPE_RANGE;
    }
    bb_catalogue_id wave=bb_catalogue_find(c,d,"wave");
    if(key(c,wave,"square"))r.dillo.wave=0;else if(key(c,wave,"sawtooth"))r.dillo.wave=1;else return BB_RECIPE_MODEL;
#define READ(object,name,scale,den,min,max,dest,optional) do {v=(dest);if(!number(c,object,name,scale,den,min,max,&v,optional))return BB_RECIPE_RANGE;(dest)=v;} while(0)
    r.dillo.slide_ms=65;r.dillo.attack_ms=3;
    READ(d,"cutoff",1,1,20,16000,r.dillo.cutoff_hz,0);
    READ(d,"resonance",100,1,0,2000,r.dillo.resonance_x100,0);
    READ(d,"env",1,1,0,100,r.dillo.envelope,0);READ(d,"decay",1,1,0,100,r.dillo.decay,0);
    READ(d,"accentAmount",1,1,0,100,r.dillo.accent,0);READ(d,"acidDrive",1,1,0,100,r.dillo.drive,1);
    READ(d,"acidAttack",1,1,1,200,r.dillo.attack_ms,1);
    r.telegram.attack_ms=6;r.telegram.release_ms=18;r.telegram.glide_ms=80;
    READ(t,"square",1,1,0,100,r.telegram.square,0);READ(t,"saw",1,1,0,100,r.telegram.saw,0);
    READ(t,"sub",1,1,0,100,r.telegram.sub,0);READ(t,"leadCutoff",1,1,20,20000,r.telegram.cutoff_hz,0);
    READ(t,"leadRes",100,1,0,2000,r.telegram.resonance_x100,0);READ(t,"leadEnv",1,1,0,100,r.telegram.envelope,0);
    READ(t,"lfoRate",100,1,0,3000,r.telegram.lfo_rate_x100,0);READ(t,"mod",1,1,0,100,r.telegram.modulation,0);
    v=0;if(!boolean(c,t,"portamento",&v,0))return BB_RECIPE_RANGE;r.telegram.glide=(uint8_t)v;
    v=0;if(!boolean(c,t,"arp",&v,0))return BB_RECIPE_RANGE;if(v)return BB_RECIPE_MODEL;
    v=0;if(!boolean(c,t,"leadAdvanced",&v,1))return BB_RECIPE_RANGE;if(v)return BB_RECIPE_MODEL;
    uint32_t transpose;
    if(!bb_catalogue_unsigned(c,bb_catalogue_find(c,t,"transpose"),0,&transpose))return BB_RECIPE_MODEL;
    /* loadEraSet resets these two levels before applying recipe overrides. */
    r.levels[0]=61;r.levels[1]=36;
    READ(d,"acidLevel",127,100,0,127,r.levels[0],1);READ(t,"leadLevel",127,100,0,127,r.levels[1],1);
#undef READ
    bb_catalogue_id g=bb_catalogue_find(c,choice->bank,"greyhoundSettings");
    if(bb_catalogue_kind(c,g)!=BB_CAT_OBJECT)return BB_RECIPE_INVALID;
    v=0;if(!boolean(c,g,"poly",&v,0))return BB_RECIPE_RANGE;
    if(v)r.capabilities|=BB_RECIPE_GREYHOUND;
    if(!bb_telegram_patch_valid(&r.telegram))return BB_RECIPE_RANGE;
    *out=r;return BB_RECIPE_OK;
}

/* 1.5: a song's own tempo and swing (settings.bpm 30..300, settings.swing 0..60 on the recipe scale, x2 = 100+swing).
 * Songs never used them before; the player applies them when a drum layer loads a section of a different song. */
int bb_factory_read_song_tempo(const bb_catalogue *c,const bb_factory_choice *choice,unsigned *bpm,unsigned *swing_x2) {
    uint32_t b,w;
    if(!c || !choice || !bpm || !swing_x2 || !bb_factory_choice_valid(c,choice) || choice->group!=BB_BROWSER_SONGS)return 0;
    bb_catalogue_id settings=bb_catalogue_find(c,choice->bank,"settings");
    if(!bb_catalogue_unsigned(c,bb_catalogue_find(c,settings,"bpm"),300,&b) || b<30)return 0;
    if(!bb_catalogue_unsigned(c,bb_catalogue_find(c,settings,"swing"),60,&w))return 0;
    *bpm=b;*swing_x2=100u+w;return 1;
}
