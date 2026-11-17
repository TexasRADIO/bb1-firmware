#include "bb_factory_launch.h"
int bb_factory_request_synth_pattern(bb_audio *audio,const bb_catalogue *catalogue,
    const bb_factory_choice *choice,unsigned synth,unsigned phrase,uint32_t *ticket) {
    if(!audio || synth>=BB_SYNTHS)return BB_FACTORY_PATTERN_INVALID;
#if BB_WITH_PATTERN
    bb_pattern_data data;
    int result=bb_factory_prepare_synth_data(catalogue,choice,synth,&data);
    if(result!=BB_FACTORY_PATTERN_OK)return result;
    if(!bb_audio_pattern_submit(audio,synth,&data,phrase,ticket))return BB_FACTORY_PATTERN_BUSY;
    return BB_FACTORY_PATTERN_OK;
#else
    (void)catalogue;(void)choice;(void)phrase;(void)ticket;
    return BB_FACTORY_PATTERN_UNAVAILABLE;
#endif
}
