#include "bb_factory_drum_launch.h"
#include "bb909_sequence.h"
#if BB_WITH_RICH_DRUM_RENDER
int bb_factory_prepare_rich_drum(const bb_catalogue *c,const bb_factory_choice *choice,bb_phrase_index *out){
    if(!out||!bb_catalogue_private(c,out,sizeof *out))return BB_FDRUM_INVALID;
    bb_factory_drum_data data;bb_phrase phrase;
    int result=bb_factory_read_drum_scratch(c,choice,&data);if(result)return result;
    if(!bb_phrase_pack_shaker(&data,&phrase)||!bb_phrase_index_prepare(&phrase,UINT32_MAX,out))return BB_FDRUM_EXPRESSION;
    return BB_FDRUM_OK;
}
int bb_factory_request_rich_drum(bb_audio *a,const bb_catalogue *c,const bb_factory_choice *choice,
    unsigned machine,unsigned boundary,uint32_t *ticket){
    if(!a||machine>=BB_RICH_DRUMS)return BB_FDRUM_INVALID;
    bb_phrase_index p;int result=bb_factory_prepare_rich_drum(c,choice,&p);if(result)return result;
    if(!bb_phrase_index_valid(&p,bb_audio_phrase_source_voices(a,machine)))return BB_FDRUM_VOICE;
    if(!bb_audio_phrase_preflight(&p))return BB_FDRUM_EXPRESSION;
    if(a->drum_patterns.family[machine])return BB_FDRUM_UNAVAILABLE;
    if(!bb_audio_phrase_submit(a,machine,&p,boundary,ticket))return BB_FDRUM_BUSY;
    return BB_FDRUM_OK;
}
#endif
int bb_factory_request_drum_pattern(bb_audio *audio,const bb_catalogue *catalogue,
    const bb_factory_choice *choice,unsigned machine,unsigned phrase,uint32_t *ticket) {
    if(!audio || machine>=BB_MACHINES)return BB_FDRUM_INVALID;
#if BB_WITH_PATTERN
    bb_factory_drum_data data;bb_factory_drum_grid grid;
    int result=bb_factory_read_drum(catalogue,choice,&data);if(result)return result;
    unsigned family=audio->drum_patterns.family[machine];
    if(family>1)return BB_FDRUM_UNAVAILABLE;
    unsigned issues=family==1?bb909_sequence_project(&data,&grid):bb_factory_project_drum(&data,&grid);
    if(issues&BB_FDRUM_ISSUE_INVALID)return BB_FDRUM_INVALID;
    if(issues&BB_FDRUM_ISSUE_TIMING)return BB_FDRUM_TIMING;
    if(issues&BB_FDRUM_ISSUE_VOICE)return BB_FDRUM_VOICE;
    if(issues)return BB_FDRUM_EXPRESSION;
    /* Factory rhythms carry required voice identities, unlike manual grids.
     * Commit those modes with the notes; preserve unrelated kit controls. */
    grid.apply_modes=(uint8_t)(family==0);
    if(!bb_audio_drum_pattern_submit(audio,machine,&grid,phrase,ticket))return BB_FDRUM_BUSY;
    return BB_FDRUM_OK;
#else
    (void)catalogue;(void)choice;(void)phrase;(void)ticket;
    return BB_FDRUM_UNAVAILABLE;
#endif
}
