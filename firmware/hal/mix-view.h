/* SPDX-License-Identifier: GPL-3.0-only */
/* Integration helper: exact retained dry/pan/send/ramp arithmetic, consuming
 * an already validated PL_VIEW. Does not release or advance the worker. */
#ifndef BB_OWNER_MIX_AUDIO_TYPE
#define BB_OWNER_MIX_AUDIO_TYPE bb_audio
#endif
static int bb_owner_mix_view(BB_OWNER_MIX_AUDIO_TYPE *a,const po_lease *l,
                             unsigned slot,unsigned k,const float *curve,float fixed,unsigned frames) {
    if(!a||!l||l->phase!=PL_VIEW||slot>=l->kits||k>=BB_MACHINES||
       !frames||frames>128u||frames!=l->frames||a->offset>128u-frames||
       !bb_rt_separate(a,sizeof *a,l,sizeof *l))return 0;
    const float *dry_bus=pl_view_bus(l,slot,0),*rev=pl_view_bus(l,slot,1),*dly=pl_view_bus(l,slot,2);
    uint32_t target=((uint32_t)a->core.drums[k].level*65536u+63u)/127u;
    uint32_t current=a->mix_gain[k];
    if(!pl_view_active(l,slot)) {
        /* Worker already advanced group faders; never advance them twice. */
        uint32_t distance=frames*256u;
        if(current<target)current+=target-current>distance?distance:target-current;
        if(current>target)current-=current-target>distance?distance:current-target;
        a->mix_gain[k]=current;return 1;
    }
    float pan_l=(k&1u)?0.16f:0.24f,pan_r=(k&1u)?0.24f:0.16f;
    unsigned offset=a->offset;
    /* Same ordered products/additions; only invariant decisions are hoisted.
     * The original ramp, curve and scope paths remain below. */
    if(current==target && !curve && a->scope.source!=BB_SCOPE_KIT_A+k) {
        float gain=(float)current*(1.0f/65536.0f);
        for(unsigned i=0;i<frames;i++) {
            float dry=dry_bus[i];
            a->left[offset+i]+=dry*pan_l*gain*fixed;
            a->right[offset+i]+=dry*pan_r*gain*fixed;
            a->echo_send[offset+i]+=dly[i]*0.2f*gain*fixed;
            a->spring_send[offset+i]+=rev[i]*0.2f*gain*fixed;
        }
        a->mix_gain[k]=current;return 1;
    }
    for(unsigned i=0;i<frames;i++) {
        float dry=dry_bus[i],gain;
        if(current<target)current+=target-current>256u?256u:target-current;
        if(current>target)current-=current-target>256u?256u:current-target;
        gain=(float)current*(1.0f/65536.0f);
        float bus=curve?curve[i]:fixed;
        if(a->scope.source==BB_SCOPE_KIT_A+k)
            a->scope.samples[offset+i]=(dry*pan_l*gain*bus+dry*pan_r*gain*bus)*0.5f;
        a->left[offset+i]+=dry*pan_l*gain*bus;
        a->right[offset+i]+=dry*pan_r*gain*bus;
        a->echo_send[offset+i]+=dly[i]*0.2f*gain*bus;
        a->spring_send[offset+i]+=rev[i]*0.2f*gain*bus;
    }
    a->mix_gain[k]=current;return 1;
}
/* Mix a complete owned pair in original global-machine order. Validate the
 * whole routing before any output write; release only after both reads end.
 * Other instruments and wet-effect processing remain the caller's job. */
static int bb_owner_mix_release(BB_OWNER_MIX_AUDIO_TYPE *a,po_lease *l,
                                const po_routing *route,const float *curve,
                                float fixed,unsigned frames) {
    if(!a||!l||!po_routing_valid(route)||route->kits!=l->kits||
       l->phase!=PL_VIEW||!frames||frames>128u||frames!=l->frames||
       a->offset>128u-frames||!bb_rt_separate(a,sizeof *a,l,sizeof *l)||
       !bb_rt_separate(a,sizeof *a,route,sizeof *route)||
       !bb_rt_separate(l,sizeof *l,route,sizeof *route))return 0;
    for(unsigned k=0;k<BB_MACHINES;k++)if(route->slot[k]!=PO_NO_SLOT) {
        if(!bb_owner_mix_view(a,l,route->slot[k],k,curve,fixed,frames))return 0;
    }
    return pl_release_view(l);
}
