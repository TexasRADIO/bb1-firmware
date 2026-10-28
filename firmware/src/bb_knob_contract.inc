/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop control descriptions: input and screen use the same entries.
 * Keep IDs identical to the audio command API; this is not an audio module. */
typedef struct {uint8_t parameter;const char *label;} bb_knob_descriptor;
#define BB_UI_UNASSIGNED 254u
#define BB_UI_LENGTH 253u
static const bb_knob_descriptor bb_synth_knobs[2][3][4]={
    {{{BB_PARAM_CUTOFF,"CUT Hz"},{BB_PARAM_RESONANCE,"RESO %"},{BB_PARAM_ENVELOPE,"ENV %"},{BB_PARAM_DECAY,"DECAY %"}},
     {{BB_PARAM_DECAY,"DECAY"},{BB_PARAM_ACCENT,"ACCENT"},{BB_PARAM_DRIVE,"DRIVE"},{BB_PARAM_SLIDE_MS,"SLIDE"}},
     {{BB_PARAM_CUTOFF,"CUT Hz"},{BB_PARAM_RESONANCE,"RESO %"},{BB_PARAM_ENVELOPE,"ENV %"},{BB_PARAM_DECAY,"DECAY %"}}},
    {{{BB_UI_LEVEL,"LEVEL"},{BB_PARAM_CUTOFF,"CUTOFF"},{BB_PARAM_RESONANCE,"RESO"},{BB_PARAM_ENVELOPE,"ENV"}},
     {{BB_PARAM_SQUARE,"SQUARE"},{BB_PARAM_SAW,"SAW"},{BB_PARAM_SUB,"SUB"},{BB_PARAM_MODULATION,"MOD"}},
     {{BB_UI_LEVEL,"LEVEL"},{BB_PARAM_LFO_RATE,"LFO"},{BB_PARAM_GLIDE,"GLIDE"},{BB_PARAM_MODULATION,"MOD"}}}
};
static const bb_knob_descriptor *bb_synth_knob(unsigned synth,unsigned page,unsigned knob){
    return synth<2u&&page<3u&&knob<4u?&bb_synth_knobs[synth][page][knob]:NULL;
}
static const bb_knob_descriptor *bb_synth_control(unsigned synth,unsigned page,unsigned knob,int rec){
    const bb_knob_descriptor *base=bb_synth_knob(synth,page,knob);
    static const bb_knob_descriptor length={BB_UI_LENGTH,"LENGTH"};
    return base&&rec&&knob==1u?&length:base;
}
static unsigned bb_synth_control_value(const bb_core *core,unsigned synth,const bb_knob_descriptor *control){
    if(!core||!control||synth>=BB_SYNTHS)return 0;
    unsigned parameter=control->parameter;
    if(parameter==BB_UI_LENGTH)return core->synths[synth].length;
    if(parameter==BB_UI_LEVEL)return core->band_level[synth+1u];
    unsigned value=bb_synth_parameter_get(core,synth,parameter);
    /* Resonance readout is knob position, not the internal x100 value or a
     * claimed acoustic Q measurement. Parameter storage/commands stay exact. */
    if(parameter==BB_PARAM_RESONANCE){
        unsigned lo,hi;
        if(!bb_synth_parameter_limits(synth,parameter,&lo,&hi)||hi<=lo)return 0;
        if(value<lo)value=lo;if(value>hi)value=hi;
        value=((value-lo)*100u+(hi-lo)/2u)/(hi-lo);
    }
    return value;
}
#if BB_WITH_EFFECTS
static const bb_knob_descriptor bb_fx_knobs[4][4]={
    {{BB_FX_ECHO_MS,"TIME MS"},{BB_FX_ECHO_FEEDBACK,"FDBK %"},{BB_FX_ECHO_HEADS,"HEADS"},{BB_FX_ECHO_WET,"WET %"}},
    {{BB_FX_ECHO_TONE,"TONE %"},{BB_FX_ECHO_DRIVE,"DRIVE %"},{BB_FX_ECHO_WEAR,"WEAR %"},{BB_FX_ECHO_WET,"WET %"}},
    {{BB_FX_SPRING_SIZE,"SIZE %"},{BB_FX_SPRING_DAMP,"DAMP %"},{BB_FX_SPRING_WET,"WET %"},{BB_FX_ECHO_WET,"ECHO %"}},
    {{BB_FX_ECHO_MOTION,"MOTION %"},{BB_FX_ECHO_WEAR,"WEAR %"},{BB_FX_ECHO_FEEDBACK,"FDBK %"},{BB_FX_ECHO_WET,"WET %"}}
};
static const bb_knob_descriptor *bb_fx_knob(unsigned bank,unsigned knob){
    return bank<4u&&knob<4u?&bb_fx_knobs[bank][knob]:NULL;
}
#endif
