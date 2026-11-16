/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase.h"
/* Main/control owner only; live immutable catalogue with its exclusive cache.
 * Resolve every source identity before publishing output. No audio/IRQ reads. */
int bb_phrase_pack_shaker(const bb_factory_drum_data *data,bb_phrase *out) {
    if(!data || !out || !data->catalogue || data->count>BB_FACTORY_DRUM_EVENTS)return 0;
    const bb_catalogue *c=data->catalogue;
    if(!bb_catalogue_private(c,data,sizeof *data) || !bb_catalogue_private(c,out,sizeof *out))return 0;
    if(bb_catalogue_kind(c,data->source)!=BB_CAT_OBJECT)return 0;
    bb_catalogue_id events=bb_catalogue_find(c,data->source,"events");
    if(bb_catalogue_kind(c,events)!=BB_CAT_ARRAY || bb_catalogue_count(c,events)!=data->count)return 0;
    uint32_t shaker[4]={0};
    for(unsigned i=0;i<data->count;i++) {
        const bb_factory_drum_event *e=&data->events[i];
        if(e->source!=bb_catalogue_at(c,events,i) || bb_catalogue_kind(c,e->source)!=BB_CAT_OBJECT)return 0;
        if(e->voice!=BB_FDV_HH)continue;
        bb_catalogue_id field=bb_catalogue_find(c,e->source,"source_voice");
        if(field==BB_CATALOGUE_NONE)continue;
        const uint8_t *text;size_t count;
        if(!bb_catalogue_text(c,field,&text,&count) || !count)return 0;
        if(count==2 && text[0]=='S' && text[1]=='H')shaker[i/32]|=UINT32_C(1)<<(i%32);
    }
    if(!bb_phrase_pack(data,out))return 0;
    for(unsigned i=0;i<data->count;i++)if(shaker[i/32]&(UINT32_C(1)<<(i%32)))
        out->events[i]=(uint16_t)((out->events[i]&~31u)|BB_PHRASE_SHAKER);
    return 1;
}
