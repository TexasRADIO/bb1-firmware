/* SPDX-License-Identifier: GPL-3.0-only
 * Derived test function from retained X0X/8W8 Big Beat renderer.
 * Original notices remain in the included drum808.c and drum808.h.
 * Exact fast-lane contribution is reused across all three buses.
 * Source SHA256: e078379b92093f716ad31dd1684838ef0cf6d854a2fa4ad58b27db0aa6559715
 */
/* Same scalar rule as public gg_voice(), kept local to remove per-sample
 * cross-object calls. Preserve the unity branch, signed zero and operation
 * order; do not reassociate a voice product with the group gain. */
static inline __attribute__((always_inline)) float bb_group_voice_exact(float contribution,float gain)
{
    return gain==1.0f?contribution:contribution*gain;
}
struct bb_span_mix { float gain,k,trim,ra,da;int bypass,type,group; };
#ifndef BB_COMMON_RAM_TARGET
#define BB_COMMON_RAM_TARGET 0
#endif
#if BB_COMMON_RAM_TARGET || defined(BB_COMMON_RAM_INLINE_TEST)
#if BB_COMMON_RAM_TARGET
#define BB_COMMON_CODE __attribute__((section(".ram_text.bb_common"),noinline,used))
#else
#define BB_COMMON_CODE __attribute__((noinline))
#endif
/* Settled, unshaped common voices only. Preserve sample-major bus aliasing,
 * addition order and exact per-voice tail state. Everything else falls back. */
static BB_COMMON_CODE void bb_common_settled_span(drum808_t *d,const struct bb_span_mix *mix,
 const float *gains,float *dry,float *rev,float *dly,int n,unsigned active,const float *increments)
{
    bb_bank_delta_cache bank_cache={{0},63u};
    /* Fixed indirect targets bridge executable RAM and flash on PI32V2.
     * Direct calls exceed the target branch displacement. No mutable global
     * target or cross-core pointer publication is introduced. */
    float (*volatile metal_cb)(d8_cb_t *,float)=cb_tick;
    float (*volatile metal_cy)(d8_cy_t *,float)=cy_tick;
#ifdef BB_COMMON_RAM_TEST_HIT
    BB_COMMON_RAM_TEST_HIT();
#endif
#ifdef BB_RICH_LANES_TEST_HIT
    if(active&((1u<<L_MA)|(1u<<L_CP)))BB_RICH_LANES_TEST_HIT(n);
#endif
    for(int i=0;i<n;i++) {
        float local_dry=dry[i],local_rev=rev[i],local_dly=dly[i],bus=0,pair=0;
        if(active&((1u<<L_CB)|(1u<<L_CH)|(1u<<L_OH)|(1u<<L_CY))) {
            bus=bb_bank_prepared_tick(&d->bank,increments,&bank_cache);
            if(active&(1u<<L_CB))
                pair=((d->bank.ph[4]<BANK_DUTY?2.5f:-2.5f)+
                      (d->bank.ph[5]<BANK_DUTY?2.5f:-2.5f))*0.5f;
        }
#define BB_COMMON_LANE(index,expression) do { \
        if(active&(1u<<(index))) { \
            const struct bb_span_mix *m=&mix[(index)]; \
            d8_lane_t *ln=&d->lane[(index)]; \
            uint8_t *alive=(uint8_t *)d+bb_context_active_offset[(index)]; \
            float v=(expression); \
            float voiced=bb_group_voice_exact(v*m->k,gains[m->group]); \
            local_dry+=voiced; \
            if(m->ra>0.0f)local_rev+=voiced*m->ra; \
            if(m->da>0.0f)local_dly+=voiced*m->da; \
            BB_COMMON_TAIL(); \
            if(!(ln->cg>0.0f && *alive))active&=~(1u<<(index)); \
        } \
    } while(0)
#if D8_TAIL_DB
#define BB_COMMON_TAIL() do { \
        float a=v<0.0f?-v:v,pk=ln->pk; \
        int32_t qn=ln->qn; \
        if(a>pk)pk=a; \
        if(a>pk*D8_TAIL_K)qn=0;else qn++; \
        ln->pk=pk;ln->qn=qn; \
        if(qn>D8_TAIL_N && pk>D8_TAIL_FLOOR)*alive=0; \
    } while(0)
#else
#define BB_COMMON_TAIL() ((void)0)
#endif
        BB_COMMON_LANE(L_BD,bb_common_bd_tick(&d->bd,1));
        BB_COMMON_LANE(L_SD,bb_common_sd_tick(&d->sd,1));
        BB_COMMON_LANE(L_MA,ma_tick(&d->ma));
        BB_COMMON_LANE(L_CP,cp_tick(&d->cp));
        BB_COMMON_LANE(L_CB,metal_cb(&d->cb,pair));
        BB_COMMON_LANE(L_CH,hat_tick(&d->ch,bus));
        BB_COMMON_LANE(L_OH,hat_tick(&d->oh,bus));
        BB_COMMON_LANE(L_CY,metal_cy(&d->cy,bus));
#undef BB_COMMON_TAIL
#undef BB_COMMON_LANE
        dry[i]=local_dry;rev[i]=local_rev;dly[i]=local_dly;
    }
}
#undef BB_COMMON_CODE
#endif
void bb_proposal_group_span(drum808_t *d, gg_state *groups, float *dry, float *rev, float *dly, int n)
{
    struct bb_span_mix mix[L_NUM];
    unsigned active=0,fast=0;
    float increments[6];
    bb_bank_delta_cache bank_cache={{0},63u};
    bb_bank_tick_fn prepared_bank_tick=bb_bank_exec_target();
    bb_hat_tick_fn prepared_hat_tick=bb_hat_exec_target();
    bb_bd_tick_fn prepared_bd_tick=bb_bd_tick_exec_target();
    bb_sd_tick_fn prepared_sd_tick=bb_sd_tick_exec_target();
    bb_tom_tick_fn prepared_tom_tick=bb_tom_tick_exec_target();
    bb_cl_tick_fn prepared_cl_tick=bb_cl_tick_exec_target();
    int l,i;
    if(n<=0)return;
    /* The watched kit is armed before audio starts and fixed for this span. */
    for(l=0;l<L_NUM;l++)if(lane_on(d,l)) {
        const d8_lane_t *ln=&d->lane[l];
        struct bb_span_mix *m=&mix[l];
        int s=ln->snd;
        active|=1u<<l;
        m->group=gg_808_sound((unsigned)s);
        m->trim=k_trim[s];
        m->gain=d->potv[s][D8P_LEVEL]*ln->hit*d->vol;
        m->k=m->trim*m->gain*ln->cg;
        m->ra=d->potv[s][D8P_REV];m->da=d->potv[s][D8P_DLY];
        m->bypass=d->shp[s].drive<1.0e-3f;
        m->type=d->pot[s][D8P_DIST];
        if(ln->cstep==0.0f && m->bypass)fast|=1u<<l;
    }
    if(active & ((1u<<L_CB)|(1u<<L_CH)|(1u<<L_OH)|(1u<<L_CY)))
        for(i=0;i<6;i++)increments[i]=(float)d->bank.inc[i];
    /* Audio owns groups for the complete event span. Settled gains cannot
     * change inside it. Moving ramps retain every original gg_frame call. */
    float gains[GG_COUNT];
    unsigned moving=0;
    for(unsigned g=0;g<GG_COUNT;g++) {
        gains[g]=groups->fader[g].current;
        moving|=groups->fader[g].remaining;
    }
    /* Keep each sample's lane addition order. No shared metal bank or
     * moving gains here; those retain the original sample-major path.
     * Reject overlapping buses because loop interchange changes alias order. */
    uintptr_t pd=(uintptr_t)dry, pr=(uintptr_t)rev, pl=(uintptr_t)dly;
    uintptr_t bytes=n<=256?(uintptr_t)n*sizeof(float):0;
    if(bytes && !moving &&
       !(active&((1u<<L_CB)|(1u<<L_CH)|(1u<<L_OH)|(1u<<L_CY))) &&
       (pd>pr?pd-pr:pr-pd)>=bytes &&
       (pd>pl?pd-pl:pl-pd)>=bytes &&
       (pr>pl?pr-pl:pl-pr)>=bytes) {
#define BB_TARGET_LANE(index, expression) do { \
        if(active&(1u<<(index))) { \
            const struct bb_span_mix *m=&mix[(index)]; \
            d8_lane_t *ln=&d->lane[(index)]; \
            uint8_t *alive=(uint8_t *)d+bb_context_active_offset[(index)]; \
            for(i=0;i<n && (active&(1u<<(index)));i++) { \
            float v=(expression); \
            float group_gain=gains[m->group]; \
            if(fast&(1u<<(index))) { \
                float voiced=bb_group_voice_exact(v*m->k,group_gain); \
                dry[i]+=voiced; \
                if(m->ra>0.0f)rev[i]+=voiced*m->ra; \
                if(m->da>0.0f)dly[i]+=voiced*m->da; \
            } else { \
                if(ln->cstep<0.0f) { \
                    ln->cg+=ln->cstep; \
                    if(ln->cg<=0.0f){ln->cg=0.0f;ln->cstep=0.0f;} \
                } \
                if(ln->cg>0.0f) { \
                    float x=v*m->trim,sv; \
                    if(!m->bypass)x=shape(x,&d->shp[ln->snd],m->type,ln->crush); \
                    sv=bb_group_voice_exact(x*m->gain*ln->cg,group_gain); \
                    dry[i]+=sv;rev[i]+=sv*m->ra;dly[i]+=sv*m->da; \
                } \
            } \
            BB_TARGET_TAIL(); \
            if(!(ln->cg>0.0f && *alive))active&=~(1u<<(index)); \
            } \
        } \
    } while(0)
#if D8_TAIL_DB
#define BB_TARGET_TAIL() do { \
        float a=v<0.0f?-v:v,pk=ln->pk; \
        int32_t qn=ln->qn; \
        if(a>pk)pk=a; \
        if(a>pk*D8_TAIL_K)qn=0;else qn++; \
        ln->pk=pk;ln->qn=qn; \
        if(qn>D8_TAIL_N && pk>D8_TAIL_FLOOR)*alive=0; \
    } while(0)
#else
#define BB_TARGET_TAIL() ((void)0)
#endif
        BB_TARGET_LANE(L_BD,prepared_bd_tick(&d->bd));
        BB_TARGET_LANE(L_SD,prepared_sd_tick(&d->sd));
        BB_TARGET_LANE(L_T0,prepared_tom_tick(&d->tom[0]));
        BB_TARGET_LANE(L_T1,prepared_tom_tick(&d->tom[1]));
        BB_TARGET_LANE(L_T2,prepared_tom_tick(&d->tom[2]));
        BB_TARGET_LANE(L_RS,rs_tick(&d->rs));
        BB_TARGET_LANE(L_CL,prepared_cl_tick(&d->cl));
        BB_TARGET_LANE(L_MA,ma_tick(&d->ma));
        BB_TARGET_LANE(L_CP,cp_tick(&d->cp));
#undef BB_TARGET_TAIL
#undef BB_TARGET_LANE
        return;
    }
#define BB_TARGET_LANE(index, expression) do { \
        if(active&(1u<<(index))) { \
            const struct bb_span_mix *m=&mix[(index)]; \
            d8_lane_t *ln=&d->lane[(index)]; \
            uint8_t *alive=(uint8_t *)d+bb_context_active_offset[(index)]; \
            float v=(expression); \
            float group_gain=gains[m->group]; \
            if(fast&(1u<<(index))) { \
                float voiced=bb_group_voice_exact(v*m->k,group_gain); \
                local_dry+=voiced; \
                if(m->ra>0.0f)local_rev+=voiced*m->ra; \
                if(m->da>0.0f)local_dly+=voiced*m->da; \
            } else { \
                if(ln->cstep<0.0f) { \
                    ln->cg+=ln->cstep; \
                    if(ln->cg<=0.0f){ln->cg=0.0f;ln->cstep=0.0f;} \
                } \
                if(ln->cg>0.0f) { \
                    float x=v*m->trim,sv; \
                    if(!m->bypass)x=shape(x,&d->shp[ln->snd],m->type,ln->crush); \
                    sv=bb_group_voice_exact(x*m->gain*ln->cg,group_gain); \
                    local_dry+=sv;local_rev+=sv*m->ra;local_dly+=sv*m->da; \
                } \
            } \
            BB_TARGET_TAIL(); \
            if(!(ln->cg>0.0f && *alive))active&=~(1u<<(index)); \
        } \
    } while(0)
#if D8_TAIL_DB
#define BB_TARGET_TAIL() do { \
        float a=v<0.0f?-v:v,pk=ln->pk; \
        int32_t qn=ln->qn; \
        if(a>pk)pk=a; \
        if(a>pk*D8_TAIL_K)qn=0;else qn++; \
        ln->pk=pk;ln->qn=qn; \
        if(qn>D8_TAIL_N && pk>D8_TAIL_FLOOR)*alive=0; \
    } while(0)
#else
#define BB_TARGET_TAIL() ((void)0)
#endif
    /* Exact dispatch specialization, not a reduced voice engine. When only
     * kick/snare/closed-hat are active, omit the ten known-inactive branches.
     * Keep sample-major ordering, moving faders, shaping, choke and tail checks.
     * Any other active voice takes the unchanged complete path below. */
    const unsigned common_mask=(1u<<L_BD)|(1u<<L_SD)|(1u<<L_CH);
#if BB_COMMON_RAM_TARGET || defined(BB_COMMON_RAM_INLINE_TEST)
    /* Maraca/shaker and clap retain independent circuit/RNG state. Include
     * them in the SAME sample-major order as the complete renderer; moving,
     * shaped, choking or other-voice spans still take that complete fallback. */
    /* Shared metallic bank advances once per sample, before all voices,
     * exactly as in the complete path. Retain CB/CH/OH/CY addition order. */
    const unsigned rich_common_mask=common_mask|(1u<<L_MA)|(1u<<L_CP)|
        (1u<<L_CB)|(1u<<L_OH)|(1u<<L_CY);
    if(!moving && !(active&~rich_common_mask) && (active&fast)==active) {
        typedef void (*bb_common_span_fn)(drum808_t *,const struct bb_span_mix *,const float *,
            float *,float *,float *,int,unsigned,const float *);
        bb_common_span_fn volatile common_target=bb_common_settled_span;
        common_target(d,mix,gains,dry,rev,dly,n,active,increments);
        return;
    }
#endif
    if(!(active&~common_mask)) {
        for(i=0;i<n;i++) {
            float local_dry=dry[i],local_rev=rev[i],local_dly=dly[i];
            if(moving)gg_frame(groups,gains);
            float bus=0;
            if(active&(1u<<L_CH))bus=prepared_bank_tick(&d->bank,increments,&bank_cache);
            BB_TARGET_LANE(L_BD,prepared_bd_tick(&d->bd));
            BB_TARGET_LANE(L_SD,prepared_sd_tick(&d->sd));
            BB_TARGET_LANE(L_CH,prepared_hat_tick(&d->ch,bus));
            dry[i]=local_dry;rev[i]=local_rev;dly[i]=local_dly;
        }
        return;
    }
    for(i=0;i<n;i++) {
        float local_dry=dry[i],local_rev=rev[i],local_dly=dly[i];
        if(moving)gg_frame(groups,gains);
        float bus=0,pair=0;
        if(active & ((1u<<L_CB)|(1u<<L_CH)|(1u<<L_OH)|(1u<<L_CY))) {
            d8_bank_t *b=&d->bank;
            bus=prepared_bank_tick(b,increments,&bank_cache);
            if(active&(1u<<L_CB))
                pair=((b->ph[4]<BANK_DUTY?2.5f:-2.5f)+(b->ph[5]<BANK_DUTY?2.5f:-2.5f))*0.5f;
        }
        BB_TARGET_LANE(L_BD,prepared_bd_tick(&d->bd));
        BB_TARGET_LANE(L_SD,prepared_sd_tick(&d->sd));
        BB_TARGET_LANE(L_T0,prepared_tom_tick(&d->tom[0]));
        BB_TARGET_LANE(L_T1,prepared_tom_tick(&d->tom[1]));
        BB_TARGET_LANE(L_T2,prepared_tom_tick(&d->tom[2]));
        BB_TARGET_LANE(L_RS,rs_tick(&d->rs));
        BB_TARGET_LANE(L_CL,prepared_cl_tick(&d->cl));
        BB_TARGET_LANE(L_MA,ma_tick(&d->ma));
        BB_TARGET_LANE(L_CP,cp_tick(&d->cp));
        BB_TARGET_LANE(L_CB,cb_tick(&d->cb,pair));
        BB_TARGET_LANE(L_CH,prepared_hat_tick(&d->ch,bus));
        BB_TARGET_LANE(L_OH,prepared_hat_tick(&d->oh,bus));
        BB_TARGET_LANE(L_CY,cy_tick(&d->cy,bus));
#undef BB_TARGET_TAIL
#undef BB_TARGET_LANE
        dry[i]=local_dry;rev[i]=local_rev;dly[i]=local_dly;
    }
}
