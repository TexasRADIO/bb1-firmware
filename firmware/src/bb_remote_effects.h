/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_REMOTE_EFFECTS_H
#define BB_REMOTE_EFFECTS_H
/* Strict bounded operands; failed parsing preserves the caller's values. */
static int bb_remote_numbers(const char *s,const char *prefix,unsigned count,unsigned *out) {
    unsigned values[3];
    if(!s||!prefix||!out||!count||count>3)return 0;
    while(*prefix)if(*s++!=*prefix++)return 0;
    for(unsigned i=0;i<count;i++) {
        unsigned value=0,digits=0;
        while(*s>='0'&&*s<='9') {
            if(++digits>3)return 0;
            value=value*10+(unsigned)(*s++-'0');
        }
        if(!digits)return 0;
        values[i]=value;
        if(i+1<count){if(*s++!=' ')return 0;}else if(*s)return 0;
    }
    for(unsigned i=0;i<count;i++)out[i]=values[i];
    return 1;
}
#endif
