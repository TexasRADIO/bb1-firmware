/* SPDX-License-Identifier: GPL-3.0-only
 * One descriptor for mixer input and readout. Instrument IDs are not row IDs. */
enum {BB_MX_DRUM,BB_MX_BUS,BB_MX_TEMPO};
typedef struct {uint8_t kind,target;const char *label;} bb_mixer_descriptor;
static const bb_mixer_descriptor *bb_mix_control(unsigned page,unsigned slot){
    static const bb_mixer_descriptor drums[]={
        {BB_MX_DRUM,0,"DRUM A"},{BB_MX_DRUM,1,"DRUM B"},
        {BB_MX_DRUM,2,"DRUM C"},{BB_MX_DRUM,3,"DRUM D"}};
    static const bb_mixer_descriptor direct[]={
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        {BB_MX_DRUM,0,"DRUM A"},{BB_MX_DRUM,1,"DRUM B"},{BB_MX_BUS,BB_MIX_DILLO,"DILLO"},
#else
        {BB_MX_BUS,BB_MIX_DRUMS,"DRUMS"},{BB_MX_BUS,BB_MIX_DILLO,"DILLO"},{BB_MX_BUS,BB_MIX_TELEGRAM,"TELE"},
#endif
        {BB_MX_TEMPO,0,"BPM"}};
    if(slot>=4u)return NULL;
    if(page==BB_MIX_KITS_PAGE){
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        if(slot>=2u)return NULL;
#endif
        return &drums[slot];
    }
    if(page!=BB_MIX_DIRECT_PAGE&&page!=BB_MIX_BAND_PAGE)return NULL;
#if !defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    if((slot==1u&&!BB_WITH_DILLO)||(slot==2u&&!BB_WITH_TELEGRAM))return NULL;
#endif
    return &direct[slot];
}
static unsigned bb_mix_value(const bb_mixer_descriptor *control){
    return control->kind==BB_MX_DRUM?bb_face.requested.drums[control->target].level:
        control->kind==BB_MX_BUS?bb_face.requested.band_level[control->target]:bb_face.requested.bpm;
}
static void bb_mixer_input(void){
    (void)bb_read_encoder(EN_PRESET);
    for(unsigned slot=0;slot<4;slot++){
        int delta=bb_read_encoder(EN_K1+slot);
        const bb_mixer_descriptor *control=bb_mix_control(bb_mix_page,slot);
        if(!delta||!control)continue;
        if(control->kind==BB_MX_TEMPO)
            bb_send(BB_CMD_TEMPO,(unsigned)bb_limit((int)bb_mix_value(control)+bb_limit(delta,-270,270),30,300));
        else bb_send_machine(control->target,control->kind==BB_MX_DRUM?BB_CMD_DRUM_LEVEL:BB_CMD_BAND_LEVEL,
            (unsigned)bb_limit((int)bb_mix_value(control)+bb_limit(delta,-127,127),0,127));
    }
}
