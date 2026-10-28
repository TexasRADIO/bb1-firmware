/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline trial: caller must own a real paused transaction before target use.
 * No flash access, command submission, or concurrent live restore. */
#if defined(BB_SAMPLE_DUAL_TRIAL)
#include "sample_scene.h"
#include "bb_note_expression.h"
#include "bb_rich_scene.h"
#include "bb_expression_scene.h"
typedef struct {
    bb_family_scene scene;
    uint8_t mix[GG_SCENE_MIX_BYTES];
    bb_sample_scene_identity identities[2];
    bb_phrase phrases[2];
    uint8_t rich_mask;
    bb_note_expression expression[BB_SYNTHS][BB_STEPS];
} bb_pair_snapshot;
/* The decoder scratch and captured metadata are never needed concurrently.
 * Re-open the small immutable envelopes after base decode, instead of keeping
 * a second family/core allocation. Failed staging is disposable. */
typedef union {bb_pair_snapshot snapshot;bb_family_scene_workspace decode;} bb_pair_stage;
#define BB_PAIR_WIRE_BYTES (BB_SAMPLE_PAIR_SCENE_BYTES+BB_RICH_SCENE_OVERHEAD+BB_EXPRESSION_SCENE_OVERHEAD)
_Static_assert(BB_PAIR_WIRE_BYTES<=3840,"paired journal payload limit");
static size_t bb_pair_encode_mode(const bb_pair_snapshot *s,void *wire,size_t capacity,int native){
    if(!s||!wire||capacity<BB_PAIR_WIRE_BYTES||!bb_wire_separated(s,sizeof *s,wire,capacity))return 0;
    uint8_t *p=wire;
    if(native&&(s->identities[0].kind!=1||s->identities[1].kind!=1))return 0;
    void *base=p+BB_RICH_SCENE_OVERHEAD+BB_EXPRESSION_SCENE_OVERHEAD;
    size_t n=native?gg_scene_encode(&s->scene,s->mix,base,BB_SAMPLE_PAIR_SCENE_BYTES):
        bb_sample_pair_scene_encode(&s->scene,s->mix,s->identities,base,BB_SAMPLE_PAIR_SCENE_BYTES);
    if(n)n=bb_expression_scene_finish(p+BB_RICH_SCENE_OVERHEAD,capacity-BB_RICH_SCENE_OVERHEAD,n,s->expression);
    if(n)n=bb_rich_scene_finish(p,capacity,n,s->phrases,s->rich_mask);
    return n;
}
static size_t bb_pair_encode(const bb_pair_snapshot *s,void *wire,size_t capacity){return bb_pair_encode_mode(s,wire,capacity,0);}
static int bb_pair_decode(const void *wire,size_t bytes,bb_pair_stage *stage,
                          uint32_t header,uint32_t payload,unsigned count){
    if(!wire||!stage||!bb_wire_separated(wire,bytes,stage,sizeof *stage))return 0;
    const void *inner,*base;size_t inner_bytes,base_bytes;
    bb_sample_scene_identity ids[2];uint8_t mix[GG_SCENE_MIX_BYTES];
    if(!bb_rich_scene_open(wire,bytes,stage->snapshot.phrases,&stage->snapshot.rich_mask,&inner,&inner_bytes)||
       !bb_expression_scene_open(inner,inner_bytes,stage->snapshot.expression,&base,&base_bytes))return 0;
    if(base_bytes>=4&&!memcmp(base,"BBKS",4)){
        if(!bb_sample_pair_scene_decode(base,base_bytes,44100,header,payload,count,&stage->decode,mix,ids))return 0;
    }else{
        /* Legacy native scenes have no sampled identity. Never infer the
         * current sampled selection from a matching pattern or bank title. */
        if(!gg_scene_decode_staged(base,base_bytes,44100,&stage->decode,mix))return 0;
        for(unsigned m=0;m<BB_MACHINES;m++)if(stage->decode.decoded.family[m])return 0;
        memset(ids,0,sizeof ids);
        for(unsigned m=0;m<2;m++){
            ids[m].kind=1;ids[m].gain=.25f;ids[m].echo=64;ids[m].spring=48;
            ids[m].pack_header_crc=header;ids[m].pack_payload_crc=payload;
        }
    }
    /* decoded and snapshot.scene have identical type and union offset0.
     * Everything after that shared scene may have been used as core scratch. */
    if(!bb_rich_scene_open(wire,bytes,stage->snapshot.phrases,&stage->snapshot.rich_mask,&inner,&inner_bytes)||
       !bb_expression_scene_open(inner,inner_bytes,stage->snapshot.expression,&base,&base_bytes))return 0;
    memcpy(stage->snapshot.mix,mix,sizeof mix);memcpy(stage->snapshot.identities,ids,sizeof ids);
    return 1;
}
static int bb_pair_capture(bb_audio *a,bb_pair_snapshot *out,const bb_single_pause *p,
                           uint32_t header_crc,uint32_t payload_crc) {
    if(!bb_single_paused(a,p)||!out||(!a->sample!=!a->sample_b)||
       (a->sample&&(!a->sample_catalogue_count||a->sample_catalogue_count>8))||
       !bb_session_state_valid(&a->core)||
       !bb_audio_groups_outside(a,out,sizeof *out)||
       !bb_sample_separate(out,sizeof *out,p,sizeof *p))return BB_SINGLE_REJECTED;
    for(unsigned m=0;m<2;m++){
        const bb_sample_audio_binding *b=bb_audio_sample_layer(a,m);
        unsigned id=bb_audio_sample_layer_id(a,m);
        if(b&&(!b->slots||!b->zones||!b->count||!b->nz||
           !(b->gain>0&&b->gain<=1)||b->echo>127||b->spring>127||
           (id!=BB_SAMPLE_808_ID&&id>=a->sample_catalogue_count)||
           !bb_sample_separate(out,sizeof *out,b,sizeof *b)||
           !bb_sample_separate(out,sizeof *out,b->slots,b->count*sizeof *b->slots)||
           !bb_sample_separate(out,sizeof *out,b->zones,b->nz*sizeof *b->zones)))return BB_SINGLE_REJECTED;
        if(b)for(unsigned z=0;z<b->nz;z++)if(!bb_sample_separate(out,sizeof *out,b->zones[z].data,b->zones[z].bytes))return BB_SINGLE_REJECTED;
        if(a->rich.valid[m]>1)return BB_SINGLE_REJECTED;
        if(a->rich.valid[m]){
            uint32_t available=!b||id==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):bb_audio_sample_source_voices(a,id);
            if(!bb_audio_phrase_preflight_playable(&a->rich.active[m],available))return BB_SINGLE_REJECTED;
        }
    }
    for(unsigned k=0;k<a->sample_catalogue_count;k++){
        const bb_sample_catalogue_entry *c=&a->sample_catalogue[k];
        if(!bb_sample_separate(out,sizeof *out,c->zones,c->nz*sizeof *c->zones))return BB_SINGLE_REJECTED;
        for(unsigned z=0;z<c->nz;z++)if(!bb_sample_separate(out,sizeof *out,c->zones[z].data,c->zones[z].bytes))return BB_SINGLE_REJECTED;
    }
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
        bb_note_expression_view view;
        if(!bb_note_expression_read(a->patterns.expression[s][n],&view))return BB_SINGLE_REJECTED;
    }
    /* This helper validates every group before writing any mix byte. No
     * fallible operation follows it; no large private snapshot on the stack. */
    if(!gg_scene_capture(a->groups,out->mix))return BB_SINGLE_REJECTED;
    memset(&out->scene,0,sizeof out->scene);out->scene.core=a->core;
    memset(out->identities,0,sizeof out->identities);
    memset(out->phrases,0,sizeof out->phrases);out->rich_mask=0;
    memcpy(out->expression,a->patterns.expression,sizeof out->expression);
    for(unsigned m=0;m<2;m++){
        const bb_sample_audio_binding *b=bb_audio_sample_layer(a,m);
        unsigned id=bb_audio_sample_layer_id(a,m);
        bb_sample_scene_identity *v=&out->identities[m];
        v->pack_header_crc=header_crc;v->pack_payload_crc=payload_crc;
        v->gain=b?b->gain:.25f;v->echo=b?b->echo:64;v->spring=b?b->spring:48;
        v->kind=!b||id==BB_SAMPLE_808_ID;v->kit=v->kind?0:id;
        if(a->rich.valid[m]){out->phrases[m]=a->rich.active[m].phrase;out->rich_mask|=1u<<m;}
    }
    return BB_SINGLE_OK;
}
static int bb_pair_restore_native(bb_audio *a,const bb_pair_snapshot *s,const bb_single_pause *p){
    if(!bb_single_paused(a,p)||!s||a->sample||a->sample_b||s->rich_mask>3||
       !bb_audio_groups_outside(a,s,sizeof *s)||!bb_sample_separate(s,sizeof *s,p,sizeof *p)||
       s->scene.core.running||s->scene.core.frame||s->scene.core.sample_rate!=44100||
       !bb_one_drum_core_ok(&s->scene.core)||bb_family_scene_admit(&s->scene,0)!=BB_FAMILY_ADMISSION_OK)return BB_SINGLE_REJECTED;
    for(unsigned m=0;m<2;m++){
        if(s->identities[m].kind!=1||s->identities[m].kit)return BB_SINGLE_REJECTED;
        if(s->rich_mask&(1u<<m)){
            bb_phrase_index index;
            if(!bb_phrase_index_prepare(&s->phrases[m],BB_RICH_REPRESENTABLE_VOICES,&index)||!bb_audio_phrase_preflight(&index))return BB_SINGLE_REJECTED;
            for(unsigned i=s->phrases[m].count;i<BB_FACTORY_DRUM_EVENTS;i++)if(s->phrases[m].events[i])return BB_SINGLE_REJECTED;
        }else{const uint8_t *q=(const uint8_t*)&s->phrases[m];for(unsigned i=0;i<sizeof s->phrases[m];i++)if(q[i])return BB_SINGLE_REJECTED;}
    }
    for(unsigned m=0;m<BB_SYNTHS;m++)for(unsigned n=0;n<BB_STEPS;n++){
        bb_note_expression_view v;if(!bb_note_expression_read(s->expression[m][n],&v))return BB_SINGLE_REJECTED;
    }
    int result=bb_single_restore_base(a,&s->scene,s->mix,p);if(result!=BB_SINGLE_OK)return result;
    if(!bb_audio_phrase_scene_install_reset(a,s->phrases,s->rich_mask))return BB_SINGLE_HELD_FAILURE;
    memcpy(a->patterns.expression,s->expression,sizeof s->expression);return BB_SINGLE_OK;
}
/* Snapshot must be decoded/staged with frame0. Pack identities come from the
 * validated resident library, never from untrusted snapshot metadata. */
static int bb_pair_restore(bb_audio *a,const bb_pair_snapshot *s,const bb_single_pause *p,
                          const bb_sample_audio_binding *ea,const bb_sample_audio_binding *eb,
                          unsigned count,uint32_t header_crc,uint32_t payload_crc){
    if(!bb_single_paused(a,p)||!s||!ea||!eb||!count||count>8||!a->sample||!a->sample_b||
       s->rich_mask>3||s->scene.core.frame||s->scene.core.running||
       s->scene.core.sample_rate!=44100||!bb_one_drum_core_ok(&s->scene.core)||
       bb_family_scene_admit(&s->scene,0)!=BB_FAMILY_ADMISSION_OK)return BB_SINGLE_REJECTED;
    bb_sample_audio_binding *owners[2]={a->sample,a->sample_b};
    const bb_sample_audio_binding *banks[2]={ea,eb};
    const void *inputs[4]={s,p,ea,eb};
    size_t sizes[4]={sizeof *s,sizeof *p,count*sizeof *ea,count*sizeof *eb};
    const void *mutable[4]={owners[0],owners[0]->slots,owners[1],owners[1]->slots};
    size_t extents[4]={sizeof *owners[0],owners[0]->count*sizeof *owners[0]->slots,
                       sizeof *owners[1],owners[1]->count*sizeof *owners[1]->slots};
    for(unsigned i=0;i<4;i++){
        if(!bb_audio_groups_outside(a,inputs[i],sizes[i]))return BB_SINGLE_REJECTED;
        for(unsigned j=0;j<4;j++)if(!bb_sample_separate(inputs[i],sizes[i],mutable[j],extents[j]))return BB_SINGLE_REJECTED;
        for(unsigned j=i+1;j<4;j++)if(!bb_sample_separate(inputs[i],sizes[i],inputs[j],sizes[j])||
           !bb_sample_separate(mutable[i],extents[i],mutable[j],extents[j]))return BB_SINGLE_REJECTED;
    }
    unsigned selected[2];
    for(unsigned m=0;m<2;m++){
        const bb_sample_scene_identity *id=&s->identities[m];
        if(id->pack_header_crc!=header_crc||id->pack_payload_crc!=payload_crc||id->kind>1||
           (id->kind?id->kit!=0:id->kit>=count)||!(id->gain>0&&id->gain<=1)||
           id->echo>127||id->spring>127)return BB_SINGLE_REJECTED;
        selected[m]=id->kind?BB_SAMPLE_808_ID:id->kit;
        const bb_phrase *phrase=(s->rich_mask&(1u<<m))?&s->phrases[m]:NULL;
        if(!phrase){const uint8_t *q=(const uint8_t*)&s->phrases[m];
            for(unsigned i=0;i<sizeof s->phrases[m];i++)if(q[i])return BB_SINGLE_REJECTED;}
        if(!bb_audio_sample_restore_check_layer(a,&s->scene.core,owners[m],banks[m],count,selected[m],m,phrase))return BB_SINGLE_REJECTED;
        for(unsigned k=0;k<count;k++){
            const bb_sample_audio_binding *b=&banks[m][k];
            /* Both owner catalogues must describe the same immutable library. */
            if(b->zones!=ea[k].zones||b->nz!=ea[k].nz||memcmp(b->lane_notes,ea[k].lane_notes,sizeof b->lane_notes))return BB_SINGLE_REJECTED;
            for(unsigned j=0;j<4;j++){
                if(!bb_sample_separate(mutable[j],extents[j],b->zones,b->nz*sizeof *b->zones))return BB_SINGLE_REJECTED;
                for(unsigned z=0;z<b->nz;z++)if(!bb_sample_separate(mutable[j],extents[j],b->zones[z].data,b->zones[z].bytes))return BB_SINGLE_REJECTED;
            }
        }
    }
    for(unsigned m=0;m<BB_SYNTHS;m++)for(unsigned n=0;n<BB_STEPS;n++){
        bb_note_expression_view v;if(!bb_note_expression_read(s->expression[m][n],&v))return BB_SINGLE_REJECTED;
    }
    /* First mutation. The caller must retain its pause on HELD_FAILURE. */
    int result=bb_single_restore_base(a,&s->scene,s->mix,p);
    if(result!=BB_SINGLE_OK)return result;
    for(unsigned m=0;m<2;m++){
        memset(owners[m]->slots,0,owners[m]->count*sizeof *owners[m]->slots);
        *owners[m]=banks[m][selected[m]==BB_SAMPLE_808_ID?0:selected[m]];
    }
    if(s->rich_mask&1){if(!bb_audio_sample_startup_phrase(a,owners[0],ea,count,selected[0],&s->phrases[0]))return BB_SINGLE_HELD_FAILURE;}
    else if(!bb_audio_sample_startup(a,owners[0],ea,count,selected[0]))return BB_SINGLE_HELD_FAILURE;
    if(s->rich_mask&2){if(!bb_audio_sample_bind_b_phrase(a,owners[1],selected[1],&s->phrases[1]))return BB_SINGLE_HELD_FAILURE;}
    else if(!bb_audio_sample_bind_b(a,owners[1],selected[1]))return BB_SINGLE_HELD_FAILURE;
    if(!bb_audio_phrase_scene_install_reset(a,s->phrases,s->rich_mask))return BB_SINGLE_HELD_FAILURE;
    for(unsigned m=0;m<2;m++){
        owners[m]->gain=s->identities[m].gain;owners[m]->echo=s->identities[m].echo;owners[m]->spring=s->identities[m].spring;
    }
    memcpy(a->patterns.expression,s->expression,sizeof s->expression);
    return BB_SINGLE_OK;
}
#endif
