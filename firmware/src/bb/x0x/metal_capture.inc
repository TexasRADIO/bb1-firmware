/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A: kit A's metal lanes (CB, CH, OH, CY + the shared bank) rendered in ONE pass with each lane's
 * per-sample terms kept apart, so another core can render them and CPU0 can add them after its own lanes in
 * the renderer's fixed CB -> CH -> OH -> CY order (TIMING-OPTIONS-R9.9.md). Same arithmetic as the complete
 * sample-major path of bb_proposal_group_span: bank tick and CB pair once per sample, then each lane's voiced
 * value, choke ramp and tail check. Never pre-sums lanes (float addition is not associative).
 * Included at the end of group_renderer.c, after group_renderer_generated.inc. */
#include "metal_capture.h"
static const int bb_metal_lane[BB_METAL_LANES] = { L_CB, L_CH, L_OH, L_CY };

void bb_metal_extract(const drum808_t *d, bb_metal_state *m)
{
    m->bank=d->bank;m->cb=d->cb;m->ch=d->ch;m->oh=d->oh;m->cy=d->cy;m->vol=d->vol;
    for(int k=0;k<BB_METAL_LANES;k++){
        const d8_lane_t *ln=&d->lane[bb_metal_lane[k]];
        int s=ln->snd;
        m->lane[k]=*ln;m->snd[k]=(uint8_t)s;
        m->level[k]=d->potv[s][D8P_LEVEL];m->rev[k]=d->potv[s][D8P_REV];m->dly[k]=d->potv[s][D8P_DLY];
        m->shp[k]=d->shp[s];m->dist[k]=d->pot[s][D8P_DIST];
        m->tune[k]=d->potv[s][D8P_TUNE];m->decay[k]=d->potv[s][D8P_DECAY];m->pot_tune[k]=d->pot[s][D8P_TUNE];
    }
    m->vel_depth=d->vel_depth;m->choke=d->choke;
}
/* A metal hit inside a CPU1 call: bb_trigger_selected's metal branch on the compact state, same arithmetic.
 * k = lane index (0 CB, 1 CH, 2 OH, 3 CY); the track is the lane's own and the sound must be the lane's metal
 * sound (the caller sends anything else to the serial path). Returns 0 for a hit it cannot represent. */
int bb_metal_trigger(bb_metal_state *m, int k, float vel)
{
    static const int track_of[BB_METAL_LANES]={D8_CB,D8_CH,D8_OH,D8_CY};
    static const int sound_of[BB_METAL_LANES]={D8S_CB,D8S_CH,D8S_OH,D8S_CY};
    if(k<0||k>=BB_METAL_LANES||m->snd[k]!=sound_of[k]||!(vel>0.0f))return 0;
    if(vel>1.0f)vel=1.0f;
    int track=track_of[k];
    float tune=m->tune[k],decay=m->decay[k];
    float vgain=SC808_FULL_VELOCITY_GAIN*(1.0f-m->vel_depth*(1.0f-vel));
    float volts=4.0f+10.0f*((vgain>1.0f?vgain:1.0f)-1.0f)/3.0f;
    if(volts<4.0f)volts=4.0f;
    if(volts>14.0f)volts=14.0f;
    float soft=vgain<1.0f?vgain:1.0f;
    d8_lane_t *ln=&m->lane[k];
    ln->hit=vgain;ln->pk=0.0f;ln->qn=0;ln->cg=1.0f;ln->cstep=0.0f;ln->snd=(uint8_t)sound_of[k];
    if(track==D8_CH && m->choke>=1){choke_lane(&m->lane[2]);hat_choke(&m->oh);}
    if(track==D8_OH && m->choke==2){choke_lane(&m->lane[1]);hat_choke(&m->ch);}
    switch(k){
    case 0: bank_ratio(&m->bank,tune);cb_ratio(&m->cb,tune);cb_trigger(&m->cb,decay,volts);ln->hit=soft;break;
    case 1: bank_ratio(&m->bank,tune);hat_trigger(&m->ch,decay,4.0f);break;
    case 2: bank_ratio(&m->bank,tune);hat_trigger(&m->oh,decay,4.0f);break;
    default: bank_ratio(&m->bank,tune);cy_ratio(&m->cy,tune,m->pot_tune[k]);cy_trigger(&m->cy,decay,4.0f);break;
    }
    return 1;
}
/* Only what the capture can change: the bank, the four voices and their lanes (parameters are read-only). */
void bb_metal_apply(drum808_t *d, const bb_metal_state *m)
{
    d->bank=m->bank;d->cb=m->cb;d->ch=m->ch;d->oh=m->oh;d->cy=m->cy;
    for(int k=0;k<BB_METAL_LANES;k++)d->lane[bb_metal_lane[k]]=m->lane[k];
}
static uint8_t *bb_metal_alive(bb_metal_state *m,int k)
{
    return k==0?&m->cb.active:k==1?&m->ch.active:k==2?&m->oh.active:&m->cy.active;
}

void bb_metal_capture_begin(bb_metal_terms *t, int frames)
{
    memset(t->pres,0,sizeof t->pres);
    t->frames=(uint16_t)frames;t->filled=0;t->nseg=0;t->any=0;
}
/* One event span of the call, samples [t->filled, t->filled+n): exactly one bb_proposal_group_span of the metal lanes.
 * 0 = no metal lane was active in it; 1 = values written; -1 = out of range */
int bb_metal_capture_seg(bb_metal_state *ms, gg_state *groups, bb_metal_terms *t, int n)
{
    struct bb_span_mix mix[BB_METAL_LANES];
    unsigned active=0,fast=0;
    float increments[6];
    bb_bank_delta_cache bank_cache={{0},63u};
    bb_bank_tick_fn prepared_bank_tick=bb_bank_exec_target();
    bb_hat_tick_fn prepared_hat_tick=bb_hat_exec_target();
    int k,i;
    const int off=t->filled;
    if(n<=0||off+n>t->frames||t->nseg>=BB_METAL_SEGS)return -1;
    const int sg=t->nseg++;
    t->seg_at[sg]=(uint8_t)off;t->seg_add[sg]=0;t->filled=(uint16_t)(off+n);
    for(k=0;k<BB_METAL_LANES;k++){
        const d8_lane_t *ln=&ms->lane[k];
        if(!(ln->cg>0.0f && *bb_metal_alive(ms,k)))continue;          /* lane_on */
        struct bb_span_mix *m=&mix[k];
        int s=ms->snd[k];
        active|=1u<<k;
        m->group=gg_808_sound((unsigned)s);
        m->trim=k_trim[s];
        m->gain=ms->level[k]*ln->hit*ms->vol;
        m->k=m->trim*m->gain*ln->cg;
        m->ra=ms->rev[k];m->da=ms->dly[k];
        m->bypass=ms->shp[k].drive<1.0e-3f;
        m->type=ms->dist[k];
        if(ln->cstep==0.0f && m->bypass)fast|=1u<<k;
        t->ra[k]=m->ra;t->da[k]=m->da;
        if(!(fast&(1u<<k))||m->ra>0.0f)t->seg_add[sg]|=(uint8_t)(1u<<k);
        if(!(fast&(1u<<k))||m->da>0.0f)t->seg_add[sg]|=(uint8_t)(0x10u<<k);
    }
    for(i=0;i<6;i++)increments[i]=(float)ms->bank.inc[i];
    float gains[GG_COUNT];
    unsigned moving=0;
    for(unsigned g=0;g<GG_COUNT;g++){gains[g]=groups->fader[g].current;moving|=groups->fader[g].remaining;}
    if(!active){   /* the faders still move with the span */
        if(moving)for(i=0;i<n;i++)gg_frame(groups,gains);
        return 0;
    }
    t->any=1;
    for(i=0;i<n;i++){
        if(moving)gg_frame(groups,gains);
        /* as the renderer: the bank advances only while a metal lane is still active (#4 6485803291) */
        if(!active)continue;
        float bus,pair=0;
        d8_bank_t *b=&ms->bank;
        bus=prepared_bank_tick(b,increments,&bank_cache);
        if(active&1u)
            pair=((b->ph[4]<BANK_DUTY?2.5f:-2.5f)+(b->ph[5]<BANK_DUTY?2.5f:-2.5f))*0.5f;
        for(k=0;k<BB_METAL_LANES;k++){
            if(!(active&(1u<<k)))continue;
            const struct bb_span_mix *m=&mix[k];
            d8_lane_t *ln=&ms->lane[k];
            uint8_t *alive=bb_metal_alive(ms,k);
            float v=k==0?cb_tick(&ms->cb,pair):k==1?prepared_hat_tick(&ms->ch,bus):
                    k==2?prepared_hat_tick(&ms->oh,bus):cy_tick(&ms->cy,bus);
            float group_gain=gains[m->group];
            float tv=0.0f;
            int has=1;
            if(fast&(1u<<k)){
                tv=bb_group_voice_exact(v*m->k,group_gain);
            }else{
                if(ln->cstep<0.0f){
                    ln->cg+=ln->cstep;
                    if(ln->cg<=0.0f){ln->cg=0.0f;ln->cstep=0.0f;}
                }
                if(ln->cg>0.0f){
                    float x=v*m->trim,sv;
                    if(!m->bypass)x=shape(x,&ms->shp[k],m->type,ln->crush);
                    sv=bb_group_voice_exact(x*m->gain*ln->cg,group_gain);
                    tv=sv;
                }else has=0;   /* the ramp reached zero: nothing added at this sample, the tail check still runs */
            }
            if(has){t->v[k][off+i]=tv;t->pres[k][(off+i)>>5]|=1u<<((off+i)&31);}
#if D8_TAIL_DB
            { float a=v<0.0f?-v:v,pk=ln->pk; int32_t qn=ln->qn;
              if(a>pk)pk=a;
              if(a>pk*D8_TAIL_K)qn=0;else qn++;
              ln->pk=pk;ln->qn=qn;
              if(qn>D8_TAIL_N && pk>D8_TAIL_FLOOR)*alive=0; }
#endif
            if(!(ln->cg>0.0f && *alive))active&=~(1u<<k);
        }
    }
    return 1;
}
/* A single-span call (the dedicated gate's form). */
int bb_metal_capture_state(bb_metal_state *ms, gg_state *groups, bb_metal_terms *t, int n)
{
    if(n<=0||n>BB_METAL_MAX)return -1;
    bb_metal_capture_begin(t,n);
    return bb_metal_capture_seg(ms,groups,t,n);
}
/* A hit CPU1 can take: what bb_metal_trigger admits. Anything else sends the call to the serial path. */
static int bb_metal_hit_ok(const bb_metal_state *m, int k, float vel)
{
    static const int sound_of[BB_METAL_LANES]={D8S_CB,D8S_CH,D8S_OH,D8S_CY};
    return k>=0&&k<BB_METAL_LANES&&m->snd[k]==sound_of[k]&&vel>0.0f;
}
/* CPU1's whole call: spans of span_frames[0..nspans), kit A's metal hits applied at their offsets (0 <= at < total,
 * non-decreasing) with bb_metal_trigger, each span split at its hit offsets as the phrase renderer splits its render.
 * Everything is checked before anything changes: -1 = not representable (more than BB_METAL_HITS hits, a hit
 * bb_metal_trigger would refuse, bad order or sizes: the caller takes the serial path), 0 = nothing to add, 1 = values. */
int bb_metal_capture_call(bb_metal_state *ms, gg_state *groups, bb_metal_terms *t,
                          const uint16_t *span_frames, int nspans, const bb_metal_hits *h)
{
    int total=0,s,j;
    if(nspans<=0||nspans>BB_METAL_SPANS||h->n>BB_METAL_HITS)return -1;
    for(s=0;s<nspans;s++){if(!span_frames[s])return -1;total+=span_frames[s];}
    if(total>BB_METAL_MAX)return -1;
    for(j=0;j<h->n;j++)
        if(h->at[j]>=total||(j&&h->at[j]<h->at[j-1])||!bb_metal_hit_ok(ms,h->k[j],h->vel[j]))return -1;
    bb_metal_capture_begin(t,total);
    for(s=0,j=0;s<nspans;s++){
        const int stop=t->filled+span_frames[s];
        while(t->filled<stop){
            for(;j<h->n&&h->at[j]==t->filled;j++)bb_metal_trigger(ms,h->k[j],h->vel[j]);
            const int to=j<h->n&&h->at[j]<stop?h->at[j]:stop;
            if(bb_metal_capture_seg(ms,groups,t,to-t->filled)<0)return -1;
        }
    }
    return t->any;
}
/* CPU0: after its own lanes, add the captured terms per sample in CB -> CH -> OH -> CY order. */
void bb_metal_merge(const bb_metal_terms *t, float *dry, float *rev, float *dly)
{
    int sg=0;
    for(int i=0;i<(int)t->frames;i++){
        while(sg+1<(int)t->nseg&&i>=t->seg_at[sg+1])sg++;
        for(int k=0;k<BB_METAL_LANES;k++){
            if(!((t->pres[k][i>>5]>>(i&31))&1u))continue;
            float v=t->v[k][i];
            dry[i]+=v;
            if((t->seg_add[sg]>>k)&1u)rev[i]+=v*t->ra[k];
            if((t->seg_add[sg]>>(k+4))&1u)dly[i]+=v*t->da[k];
        }
    }
}

/* CPU0 helpers for fix A's wiring in bb_audio.c, which cannot see the lane indexes. */
/* All four metal lanes carry their own metal sound: bb_metal_trigger refuses anything else. */
int bb_metal_ready(const drum808_t *d)
{
    static const int sound_of[BB_METAL_LANES]={D8S_CB,D8S_CH,D8S_OH,D8S_CY};
    for(int k=0;k<BB_METAL_LANES;k++)if(d->lane[bb_metal_lane[k]].snd!=sound_of[k])return 0;
    return 1;
}
/* FNV-1a over every kit A setting the capture reads (bb_metal_extract's parameter fields, by the lanes' sounds). */
uint32_t bb_metal_params(const drum808_t *d)
{
    uint32_t h=UINT32_C(2166136261);
#define BB_METAL_H(p,n) do{const uint8_t *q_=(const uint8_t *)(p);for(unsigned i_=0;i_<(unsigned)(n);i_++)h=(h^q_[i_])*UINT32_C(16777619);}while(0)
    for(int k=0;k<BB_METAL_LANES;k++){
        const int s=d->lane[bb_metal_lane[k]].snd;
        BB_METAL_H(&s,sizeof s);BB_METAL_H(d->potv[s],sizeof d->potv[s]);BB_METAL_H(d->pot[s],sizeof d->pot[s]);
        BB_METAL_H(&d->shp[s],sizeof d->shp[s]);
    }
    BB_METAL_H(&d->vol,sizeof d->vol);BB_METAL_H(&d->vel_depth,sizeof d->vel_depth);BB_METAL_H(&d->choke,sizeof d->choke);
#undef BB_METAL_H
    return h;
}
/* CPU1 owns kit A's metal for the call: keep CPU0's lanes, then silence them on CPU0 (lane_on needs cg > 0, so the
 * renderer drops the metal lanes AND the shared bank tick). bb_metal_apply replaces them on success. */
void bb_metal_park(drum808_t *d, d8_lane_t saved[BB_METAL_LANES])
{
    for(int k=0;k<BB_METAL_LANES;k++){saved[k]=d->lane[bb_metal_lane[k]];d->lane[bb_metal_lane[k]].cg=0.0f;}
}
/* Any failure: put CPU0's lanes back. With cg = 0 CPU0 never ticked those voices, so they are as parked. */
void bb_metal_unpark(drum808_t *d, const d8_lane_t saved[BB_METAL_LANES])
{
    for(int k=0;k<BB_METAL_LANES;k++)d->lane[bb_metal_lane[k]]=saved[k];
}
