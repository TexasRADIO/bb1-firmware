/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_REMOTE_LIBRARY_H
#define BB_REMOTE_LIBRARY_H
/* Strict decimal operands: no signs, overflow, trailing tokens or wrapping.
 * This parser never changes its output on failure. Catalogue bounds are
 * checked separately against the live immutable catalogue. */
typedef struct { unsigned layer, kit, bank, part; } bb_remote_selection;
static int bb_remote_selection_parse(const char *text, bb_remote_selection *out) {
    static const char prefix[]="bbtest song ";
    unsigned values[4], limits[4]={1,3,65535,65535};
    unsigned i;
    if(!text || !out)return 0;
    for(i=0;i<sizeof(prefix)-1;i++)if(text[i]!=prefix[i])return 0;
    text+=sizeof(prefix)-1;
    for(i=0;i<4;i++) {
        unsigned value=0, digits=0;
        while(*text>='0' && *text<='9') {
            unsigned digit=(unsigned)(*text-'0');
            if(value>limits[i]/10 ||
               (value==limits[i]/10 && digit>limits[i]%10))return 0;
            value=value*10+digit; ++text; ++digits;
            if(digits>5)return 0;
        }
        if(!digits)return 0;
        values[i]=value;
        if(i<3) { if(*text!=' ')return 0; ++text; }
        else if(*text)return 0;
    }
    bb_remote_selection result={values[0],values[1],values[2],values[3]};
    *out=result;return 1;
}
#endif
