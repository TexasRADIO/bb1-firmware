/* SPDX-License-Identifier: GPL-3.0-only */
/* Include in the drum translation unit after bb_trigger_selected. */
#include "bb_source_trigger.h"
#include "bb_phrase.h"
#include "bb_voice_fallback.h"
int bb_drum808_source_trigger(drum808_t *d,unsigned voice,float velocity)
{
    static const unsigned char track[16]={D8_BD,D8_SD,D8_LT,D8_MT,D8_HT,
        D8_LT,D8_MT,D8_HT,D8_RS,D8_RS,D8_CP,D8_CP,D8_CB,D8_CY,D8_OH,D8_CH};
    static const unsigned char sound[16]={D8S_BD,D8S_SD,D8S_LT,D8S_MT,D8S_HT,
        D8S_LC,D8S_MC,D8S_HC,D8S_RS,D8S_CL,D8S_CP,D8S_MA,D8S_CB,D8S_CY,D8S_OH,D8S_CH};
    if(!d || !(velocity>0.f && velocity<=1.f))return 0;
    voice=bb_voice_fallback(voice,UINT32_C(65535)|(UINT32_C(1)<<BB_PHRASE_SHAKER));
    if(voice==BB_PHRASE_SHAKER){bb_trigger_selected(d,D8_CP,D8S_MA,velocity);return 1;}
    if(voice>=16)return 0;
    bb_trigger_selected(d,track[voice],sound[voice],velocity);
    return 1;
}
