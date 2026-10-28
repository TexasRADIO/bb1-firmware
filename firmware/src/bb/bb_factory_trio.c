/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_factory_trio.h"
#include "bb_session.h"
#include <string.h>
#if BB_TRIO_PARTS != 8 || BB_SYNTHS != 2
#error "Era assembler requires an explicit migration for changed part/synth dimensions"
#endif
static int separated(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int letter(const bb_catalogue *c,bb_catalogue_id part,unsigned index) {
    const uint8_t *p;size_t n;
    return bb_catalogue_text(c,bb_catalogue_find(c,part,"letter"),&p,&n) && n==1 && p[0]=='A'+index;
}
static bb_factory_choice section(const bb_catalogue *c,const bb_factory_choice *bank,unsigned index) {
    bb_factory_choice part=*bank;part.part_index=(uint16_t)index;
    part.part=bb_catalogue_at(c,bb_catalogue_find(c,bank->bank,"parts"),index);return part;
}
static int native_inheritance(const bb_core *c,unsigned primary) {
    if(!bb_session_state_valid(c) || c->sample_rate<8000 || c->sample_rate>192000)return 0;
    const uint16_t voices=(1u<<BB_DRUM_TRACKS)-1u;
    for(unsigned d=0;d<BB_MACHINES;d++)if(d!=primary)for(unsigned n=0;n<BB_STEPS;n++)
        if((c->drums[d].steps[n].hits&~voices) || (c->drums[d].steps[n].accents&~c->drums[d].steps[n].hits))return 0;
    return 1;
}
int bb_factory_prepare_trio(const bb_catalogue *c,const bb_factory_choice *choice,
    const bb_core *inherited,unsigned primary,unsigned policy,bb_trio_program *out,
    bb_factory_trio_workspace *work,bb_factory_trio_report *report) {
    if(!c || !choice || !inherited || !out || !work || !report)return BB_FTRIO_INVALID;
    const void *regions[]={c,choice,inherited,out,work,report,c->container.bytes,c->container.cache};
    const size_t sizes[]={sizeof *c,sizeof *choice,sizeof *inherited,sizeof *out,sizeof *work,sizeof *report,c->container.length,sizeof *c->container.cache};
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<i;j++)
        if(!separated(regions[i],sizes[i],regions[j],sizes[j]))return BB_FTRIO_INVALID;
    if(primary>=BB_MACHINES || policy&~BB_FTRIO_THREE_ENGINE ||
       !bb_factory_choice_valid(c,choice) || choice->group!=BB_BROWSER_ERAS ||
       !native_inheritance(inherited,primary))return BB_FTRIO_INVALID;
    bb_factory_trio_report r;memset(&r,0,sizeof r);r.primary=(uint8_t)primary;r.initial=(uint8_t)choice->part_index;
    r.recipe_result=(uint8_t)bb_factory_read_recipe(c,choice,&work->recipe);
    if(r.recipe_result){*report=r;return BB_FTRIO_RECIPE;}
    r.capabilities=work->recipe.capabilities;
    /* Target clock is whole-percent: report unsupported timing before any
     * second-pass scene construction, never round or write core padding. */
    if(r.capabilities&BB_RECIPE_SWING_RANGE){*report=r;return BB_FTRIO_SWING;}
    bb_catalogue_id parts=bb_catalogue_find(c,choice->bank,"parts");
    if(bb_catalogue_kind(c,parts)!=BB_CAT_ARRAY || bb_catalogue_count(c,parts)!=BB_TRIO_PARTS) {
        *report=r;return BB_FTRIO_LAYOUT;
    }
    unsigned rejected=0;
    for(unsigned slot=0;slot<BB_TRIO_PARTS;slot++) {
        bb_factory_choice part=section(c,choice,slot);bb_factory_trio_part_report *p=&r.part[slot];p->source=part.part;
        if(!letter(c,part.part,slot)){*report=r;return BB_FTRIO_LAYOUT;}
        p->drum_result=(uint8_t)bb_factory_read_drum(c,&part,&work->parsed.drum.data);
        if(!p->drum_result) {
            p->drum_issues=(uint16_t)bb_factory_project_drum(&work->parsed.drum.data,&work->parsed.drum.grid);
            if(!p->drum_issues) {
                const bb_factory_drum_grid *g=&work->parsed.drum.grid;
                if(g->length<16 || g->length%16) {p->drum_result=BB_FDRUM_TIMING;}
                else p->phrase=g->length;
                for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)if(g->mode_mask&(1u<<lane))
                    if(inherited->drums[primary].sound.mode[lane]!=((g->mode_value>>lane)&1u))p->mode_mismatch|=(uint16_t)(1u<<lane);
            }
        }
        for(unsigned synth=0;synth<BB_SYNTHS;synth++)
            p->synth_result[synth]=(uint8_t)bb_factory_prepare_synth_data(c,&part,synth,&work->parsed.synth);
        /* Keep mismatch report; prepared bank already applies each section mode mask. */
        rejected|=p->drum_result || p->drum_issues || p->synth_result[0] || p->synth_result[1];
    }
    *report=r;
    if(rejected)return BB_FTRIO_CONTENT;
    if((r.capabilities&BB_RECIPE_GREYHOUND) && !(policy&BB_FTRIO_THREE_ENGINE))return BB_FTRIO_GREYHOUND;
    /* This is exclusively owned staging, not a live scene. No second 6KB copy.
     * Unexpected second-pass failure must still cause caller lease abort. */
    memset(out,0,sizeof *out);out->seed=*inherited;out->primary=(uint8_t)primary;out->initial=r.initial;
    out->seed.dillo=work->recipe.dillo;out->seed.telegram=work->recipe.telegram;
    out->seed.band_level[BB_MIX_DILLO]=work->recipe.levels[0];
    out->seed.band_level[BB_MIX_TELEGRAM]=work->recipe.levels[1];
    out->seed.synths[0].muted=out->seed.synths[1].muted=0;
    out->seed.bpm=work->recipe.bpm;
    if(!bb_set_swing_x2(&out->seed,work->recipe.swing_x2))return BB_FTRIO_SWING;
    /* The primary seed grid is unused; every part supplies its replacement. */
    memset(out->seed.drums[primary].steps,0,sizeof out->seed.drums[primary].steps);
    for(unsigned slot=0;slot<BB_TRIO_PARTS;slot++) {
        bb_factory_choice part=section(c,choice,slot);bb_trio_part *p=&out->part[slot];
        if(bb_factory_read_drum(c,&part,&work->parsed.drum.data) ||
           bb_factory_project_drum(&work->parsed.drum.data,&work->parsed.drum.grid))return BB_FTRIO_CONTENT;
        const bb_factory_drum_grid *grid=&work->parsed.drum.grid;
        memcpy(p->drums,grid->steps,sizeof p->drums);p->drum_length=grid->length;
        p->mode_mask=grid->mode_mask;p->mode_value=grid->mode_value;p->phrase=r.part[slot].phrase;
        for(unsigned synth=0;synth<BB_SYNTHS;synth++) {
            if(bb_factory_prepare_synth_data(c,&part,synth,&work->parsed.synth))return BB_FTRIO_CONTENT;
            p->synth_length[synth]=work->parsed.synth.length;
            for(unsigned n=0;n<BB_STEPS;n++)if(!bb_trio_pack_note(work->parsed.synth.notes[n],&p->notes[synth][n]))return BB_FTRIO_CONTENT;
        }
    }
    return bb_trio_valid(out)?BB_FTRIO_OK:BB_FTRIO_INVALID;
}
