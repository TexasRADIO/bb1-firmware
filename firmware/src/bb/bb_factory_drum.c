#include "bb_factory_drum.h"
#include <string.h>
static const char *const voices[]={"BD","SD","LT","MT","HT","LC","MC","HC","RS","C",
    "CP","MA","CB","CY","OHH","HH","RD","SY","ZAP","ACCENT","ACC"};
const char *bb_factory_drum_voice_name(unsigned n) {return n<BB_FDV_COUNT?voices[n]:NULL;}
static int text_is(const bb_catalogue *c,bb_catalogue_id id,const char *wanted) {
    const uint8_t *bytes;size_t count,n=0;while(wanted[n])n++;
    return bb_catalogue_text(c,id,&bytes,&count) && count==n && !memcmp(bytes,wanted,n);
}
static int nonempty(const bb_catalogue *c,bb_catalogue_id id) {
    const uint8_t *bytes;size_t count;return bb_catalogue_text(c,id,&bytes,&count) && count;
}
static int number(const bb_catalogue *c,bb_catalogue_id id,unsigned max,unsigned *out) {
    uint32_t n;if(!bb_catalogue_unsigned(c,id,max,&n))return 0;*out=(unsigned)n;return 1;
}
static int decoded_is(const uint8_t *bytes,size_t count,const char *wanted) {
    size_t n=0;while(wanted[n])n++;
    return count==n && !memcmp(bytes,wanted,n);
}
static int voice_id(const bb_catalogue *c,bb_catalogue_id id) {
    const uint8_t *bytes;size_t count;
    if(!bb_catalogue_text(c,id,&bytes,&count))return -1;
    for(unsigned i=0;i<BB_FDV_COUNT;i++)if(decoded_is(bytes,count,voices[i]))return (int)i;
    return -1;
}
static int field_is(const bb_catalogue *c,bb_catalogue_id id,const char *const *names,size_t count) {
    const uint8_t *bytes;size_t length;
    if(!bb_catalogue_text(c,id,&bytes,&length))return 0;
    for(size_t i=0;i<count;i++)if(decoded_is(bytes,length,names[i]))return 1;
    return 0;
}
int bb_factory_read_drum(const bb_catalogue *c,const bb_factory_choice *choice,bb_factory_drum_data *out) {
    if(!out || !bb_catalogue_private(c,out,sizeof *out))return BB_FDRUM_INVALID;
    bb_factory_drum_data data;
    int result=bb_factory_read_drum_scratch(c,choice,&data);
    if(!result)*out=data;
    return result;
}
int bb_factory_read_drum_scratch(const bb_catalogue *c,const bb_factory_choice *choice,bb_factory_drum_data *out) {
    if(!out || !bb_catalogue_private(c,out,sizeof *out) || !bb_catalogue_private(c,choice,sizeof *choice) ||
       !bb_factory_choice_valid(c,choice))return BB_FDRUM_INVALID;
    /* Preserve choice even when caller's private scratch aliases it. */
    bb_factory_choice saved_choice=*choice;choice=&saved_choice;
    bb_catalogue_id pattern=bb_catalogue_find(c,choice->part,"pattern");
    if(bb_catalogue_kind(c,pattern)!=BB_CAT_OBJECT)return BB_FDRUM_INVALID;
    static const char *const fields[]={"id","label","category","meter","native_steps","lattice",
                                     "phraseBars","events","era","library"};
    for(unsigned i=0;i<bb_catalogue_count(c,pattern);i++)
        if(!field_is(c,bb_catalogue_key_at(c,pattern,i),fields,sizeof fields/sizeof fields[0]))return BB_FDRUM_FIELD;
    for(unsigned i=0;i<3;i++)if(!nonempty(c,bb_catalogue_find(c,pattern,fields[i])))return BB_FDRUM_INVALID;
    if(!text_is(c,bb_catalogue_find(c,pattern,"meter"),"4/4"))return BB_FDRUM_TIMING;
    memset(out,0,sizeof *out);out->catalogue=c;out->source=pattern;
    unsigned length,declared;
    if(!number(c,bb_catalogue_find(c,pattern,"native_steps"),BB_STEPS,&length) || !length ||
       !number(c,bb_catalogue_find(c,bb_catalogue_find(c,choice->part,"lengths"),"drums"),BB_STEPS,&declared) ||
       declared!=length)return BB_FDRUM_LENGTH;
    out->length=(uint8_t)length;
    static const char *const lattices[]={"sixteenths_in_4_4","eighths_in_4_4","sixteenth_triplets_in_4_4"};
    unsigned lattice;
    for(lattice=0;lattice<3;lattice++)if(text_is(c,bb_catalogue_find(c,pattern,"lattice"),lattices[lattice]))break;
    if(lattice==3)return BB_FDRUM_TIMING;out->lattice=(uint8_t)lattice;
    bb_catalogue_id bars=bb_catalogue_find(c,pattern,"phraseBars");
    if(bars!=BB_CATALOGUE_NONE) {
        unsigned n;if(!number(c,bars,4,&n) || !n)return BB_FDRUM_TIMING;out->bars=(uint8_t)n;
        unsigned per_bar=lattice==BB_FDL_SIXTEENTHS?16u:lattice==BB_FDL_EIGHTHS?8u:24u;
        if(length!=n*per_bar)return BB_FDRUM_TIMING;
    }
    bb_catalogue_id events=bb_catalogue_find(c,pattern,"events");
    size_t count=bb_catalogue_count(c,events);
    if(bb_catalogue_kind(c,events)!=BB_CAT_ARRAY)return BB_FDRUM_INVALID;
    if(count>BB_FACTORY_DRUM_EVENTS)return BB_FDRUM_LENGTH;out->count=(uint8_t)count;
    static const char *const event_fields[]={"voice","step","role","source_voice","source_role","intensity","modifiers"};
    for(unsigned i=0;i<count;i++) {
        bb_catalogue_id item=bb_catalogue_at(c,events,i);
        if(bb_catalogue_kind(c,item)!=BB_CAT_OBJECT)return BB_FDRUM_INVALID;
        bb_catalogue_id values[7];
        for(unsigned k=0;k<7;k++)values[k]=BB_CATALOGUE_NONE;
        size_t fields_count=bb_catalogue_count(c,item);
        for(unsigned k=0;k<fields_count;k++) {
            bb_catalogue_id key=bb_catalogue_key_at(c,item,k);
            const uint8_t *bytes;size_t length;
            if(!bb_catalogue_text(c,key,&bytes,&length))return BB_FDRUM_FIELD;
            if(decoded_is(bytes,length,"velocity") || decoded_is(bytes,length,"offset"))return BB_FDRUM_EXPRESSION;
            unsigned field;
            for(field=0;field<7;field++)if(decoded_is(bytes,length,event_fields[field]))break;
            if(field==7)return BB_FDRUM_FIELD;
            values[field]=bb_catalogue_at(c,item,k);
        }
        bb_factory_drum_event *event=&out->events[i];event->source=item;
        int voice=voice_id(c,values[0]);
        if(voice<0)return BB_FDRUM_VOICE;event->voice=(uint8_t)voice;
        unsigned step;if(!number(c,values[1],length,&step) || !step)return BB_FDRUM_LENGTH;
        event->step=(uint8_t)step;
        const char *role=voice>=BB_FDV_ACCENT?"accent_control":"playable_voice";
        if(!text_is(c,values[2],role))return BB_FDRUM_INVALID;
        for(unsigned k=3;k<=4;k++) {
            bb_catalogue_id id=values[k];
            if(id!=BB_CATALOGUE_NONE && !nonempty(c,id))return BB_FDRUM_INVALID;
        }
        bb_catalogue_id intensity=values[5];
        if(intensity!=BB_CATALOGUE_NONE) {
            if(text_is(c,intensity,"accented"))event->intensity=BB_FDI_ACCENTED;
            else if(text_is(c,intensity,"unaccented"))event->intensity=BB_FDI_UNACCENTED;
            else return BB_FDRUM_EXPRESSION;
        }
        bb_catalogue_id mods=values[6];
        if(mods!=BB_CATALOGUE_NONE) {
            if(bb_catalogue_kind(c,mods)!=BB_CAT_ARRAY)return BB_FDRUM_INVALID;
            for(unsigned k=0;k<bb_catalogue_count(c,mods);k++) {
                bb_catalogue_id mod=bb_catalogue_at(c,mods,k);unsigned bit;
                if(text_is(c,mod,"flam"))bit=BB_FDM_FLAM;
                else if(text_is(c,mod,"retrigger_rate_or_fraction"))bit=BB_FDM_RETRIGGER_RATE;
                else if(text_is(c,mod,"retrigger_length"))bit=BB_FDM_RETRIGGER_LENGTH;
                else return BB_FDRUM_EXPRESSION;
                if(event->modifiers&bit)return BB_FDRUM_INVALID;event->modifiers|=(uint8_t)bit;
            }
        }
    }
    return BB_FDRUM_OK;
}
unsigned bb_factory_project_drum(const bb_factory_drum_data *data,bb_factory_drum_grid *out) {
    if(!data || !out || !data->length || data->length>BB_STEPS || data->count>BB_FACTORY_DRUM_EVENTS ||
       data->lattice>BB_FDL_TRIPLETS || data->bars>4)return BB_FDRUM_ISSUE_INVALID;
    static const int8_t lane[]={0,1,2,3,4,2,3,4,5,5,6,6,7,8,9,10,-1,-1,-1,-1,-1};
    static const uint8_t alternate[]={0,0,0,0,0,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0};
    unsigned scale=data->lattice==BB_FDL_EIGHTHS?2u:1u,issues=0;
    if(data->lattice==BB_FDL_TRIPLETS || data->length*scale>BB_STEPS)issues|=BB_FDRUM_ISSUE_TIMING;
    uint32_t seen[BB_STEPS]={0};uint8_t accent[BB_STEPS]={0};
    bb_factory_drum_grid grid;memset(&grid,0,sizeof grid);
    grid.length=(uint8_t)(data->length*scale);
    for(unsigned i=0;i<data->count;i++) {
        const bb_factory_drum_event *event=&data->events[i];
        if(event->voice>=BB_FDV_COUNT || !event->step || event->step>data->length ||
           event->intensity>BB_FDI_UNACCENTED || event->modifiers&~7u)return BB_FDRUM_ISSUE_INVALID;
        unsigned raw=event->step-1u,step=raw*scale;
        uint32_t bit=UINT32_C(1)<<event->voice;
        if(seen[raw]&bit)issues|=BB_FDRUM_ISSUE_DUPLICATE;seen[raw]|=bit;
        if(event->modifiers)issues|=BB_FDRUM_ISSUE_MODIFIER;
        if(event->voice>=BB_FDV_ACCENT) {
            if(step<BB_STEPS)accent[step]=1;
            continue;
        }
        int track=lane[event->voice];
        if(track<0){issues|=BB_FDRUM_ISSUE_VOICE;continue;}
        uint16_t required=(uint16_t)(1u<<(unsigned)track),value=alternate[event->voice]?required:0;
        if((grid.mode_mask&required) && (grid.mode_value&required)!=value)issues|=BB_FDRUM_ISSUE_LANE;
        grid.mode_mask|=required;grid.mode_value|=value;
        if(step<BB_STEPS) {
            grid.steps[step].hits|=required;
            if(event->intensity==BB_FDI_ACCENTED)grid.steps[step].accents|=required;
        }
    }
    for(unsigned step=0;step<BB_STEPS;step++)if(accent[step])grid.steps[step].accents|=grid.steps[step].hits;
    if(issues)return issues;*out=grid;return 0;
}
int bb_factory_stage_drum(const bb_catalogue *c,const bb_factory_choice *choice,unsigned machine,bb_core *staging) {
    if(!staging || machine>=BB_MACHINES)return BB_FDRUM_INVALID;
    bb_factory_drum_data data;bb_factory_drum_grid grid;
    int result=bb_factory_read_drum(c,choice,&data);if(result)return result;
    unsigned issues=bb_factory_project_drum(&data,&grid);
    if(issues&BB_FDRUM_ISSUE_TIMING)return BB_FDRUM_TIMING;
    if(issues&BB_FDRUM_ISSUE_VOICE)return BB_FDRUM_VOICE;
    if(issues)return BB_FDRUM_EXPRESSION;
    for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)if(grid.mode_mask&(1u<<lane))
        if(staging->drums[machine].sound.mode[lane]!=((grid.mode_value>>lane)&1u))return BB_FDRUM_MODE;
    memcpy(staging->drums[machine].steps,grid.steps,sizeof grid.steps);
    staging->drums[machine].length=grid.length;staging->drums[machine].next=0;return BB_FDRUM_OK;
}
