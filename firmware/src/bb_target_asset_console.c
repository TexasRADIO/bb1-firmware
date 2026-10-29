/* SPDX-License-Identifier: GPL-3.0-only */
/* Included in the main console after storage and audio definitions. */
#include "bb_asset_transfer.h"
static int bb_session_busy(void);
static bb_asset_transfer bb_asset_tx;
static uint32_t bb_asset_mode, bb_asset_bytes, bb_asset_expected, bb_asset_header_expected;
static uint8_t bb_asset_locked;
static int bb_asset_read(void *ctx,uint32_t off,void *out,unsigned n) {
    (void)ctx; fm1_wdt_feed(); return st_read(off,out,n);
}
static int bb_asset_erase(void *ctx,uint32_t off) {
    uint32_t took; (void)ctx; fm1_wdt_feed(); return st_erase_dark(off,&took); /* LED rows dark (storage_hw.c) */
}
static int bb_asset_program(void *ctx,uint32_t off,const void *data,unsigned n) {
    (void)ctx; fm1_wdt_feed(); return fl_write(off,data,n);
}
static int bb_asset_num(const char **p,uint32_t *out) {
    const char *s=*p;uint32_t v=0;unsigned n=0;
    while(*s==' ')s++;
    if(s[0]=='0' && (s[1]=='x'||s[1]=='X'))s+=2;
    for(;;s++) {
        unsigned d;
        if(*s>='0'&&*s<='9')d=(unsigned)(*s-'0');
        else if(*s>='a'&&*s<='f')d=(unsigned)(*s-'a')+10u;
        else if(*s>='A'&&*s<='F')d=(unsigned)(*s-'A')+10u;
        else break;
        if(n==8u)return 0;v=(v<<4)|d;n++;
    }
    if(!n || (*s && *s!=' '))return 0;*p=s;*out=v;return 1;
}
static int bb_asset_end(const char *p) {while(*p==' ')p++;return !*p;}
static int bb_asset_safe(void) {
    return flash_ok && !bb_sound.core.running && bb_commands_idle(&bb_sound.commands)
#if BB_WITH_PREPARED_BANK
        && pb_ownership_idle(&bb_sound.parts)
#endif
        && !song.master_q12 && !uac.alt && !bb_session_busy();
}
static int bb_asset_ready(void) {
    if(!bb_asset_locked || !bb_asset_hold_ack)return 0;
    if(bb_c1_live.busy)return 0;
    bb_c1_halt();BB_C1_SYNC();
    if((BB_C1_CON&10u)!=2u)return 0;
    bb_c1_live.ready=0;bb_c1_live.held=1;bb_c1_report.held=1;
    return 1;
}
static void bb_asset_screen(void) {
    lcd_fill(0,0,240,240,T_BG);
    draw_text_box(0,50,240,&AF_M,"ASSET UPDATE",T_TEXT,1);
    draw_text_box(0,90,240,&AF_S,"AUDIO PAUSED",T_TEXT,1);
    draw_text_box(0,130,240,&AF_S,"KEEP USB CONNECTED",T_TEXT,1);
    lcd_sync();
}
static void bb_asset_reply(unsigned rc) {
    con_kv("asset_rc",(int32_t)rc);con_kv("asset_state",(int32_t)bb_asset_tx.state);
    con_kv("asset_next",(int32_t)bb_asset_tx.next);con_kv("asset_held",bb_asset_ready());
    con_kv("asset_sample_v",1);con_kv("asset_bytes",(int32_t)bb_asset_tx.bytes);
}
static int bb_asset_command(const char *p) {
    unsigned rc=2;
    int sampled=con_word(&p,"bsinit");
    if(sampled||con_word(&p,"bainit")) {
        uint32_t mode,bytes,expected,header;
        if(sampled){
            mode=BB_ASSET_SAMPLE_INSTALL;
            if(!bb_asset_num(&p,&bytes)||bytes<800||bytes>BB_ASSET_REGION_BYTES)goto reply;
        }else{
            if(!bb_asset_num(&p,&mode)||mode>1u)goto reply;
            bytes=mode?BB_ASSET_REGION_BYTES:BB_ASSET_PACK_BYTES;
        }
        if(!bb_asset_num(&p,&expected)||!bb_asset_num(&p,&header)||!bb_asset_end(p))goto reply;
        if(bb_asset_locked && (mode!=bb_asset_mode||bytes!=bb_asset_bytes||expected!=bb_asset_expected||header!=bb_asset_header_expected))goto reply;
        uintptr_t token=bb_target_enter(0);
        int safe=bb_asset_safe();
        if(safe && !bb_asset_locked) {
            bb_asset_mode=mode;bb_asset_bytes=bytes;bb_asset_expected=expected;bb_asset_header_expected=header;
            bb_asset_locked=1;bb_asset_hold_ack=0;bb_asset_hold_request=1;
        }
        bb_target_leave(0,token);
        if(!safe)goto reply;
        if(!bb_asset_ready()){rc=1;goto reply;}
        const bb_asset_io io={0,bb_asset_read,bb_asset_erase,bb_asset_program};
        rc=bb_asset_begin(&bb_asset_tx,&io,mode,bytes,expected,header)?0u:2u;
        bb_asset_screen();goto reply;
    }
    if(con_word(&p,"baw")) {
        uint32_t off,crc;uint8_t data[32];unsigned n=0;
        if(!bb_asset_num(&p,&off)||!bb_asset_num(&p,&crc))goto reply;
        while(*p==' ')p++;
        while(*p) {
            if(n==sizeof data || !p[1])goto reply;
            unsigned a,b;char x=*p++,y=*p++;
            if(x>='0'&&x<='9')a=(unsigned)(x-'0');else if(x>='A'&&x<='F')a=(unsigned)(x-'A')+10;else if(x>='a'&&x<='f')a=(unsigned)(x-'a')+10;else goto reply;
            if(y>='0'&&y<='9')b=(unsigned)(y-'0');else if(y>='A'&&y<='F')b=(unsigned)(y-'A')+10;else if(y>='a'&&y<='f')b=(unsigned)(y-'a')+10;else goto reply;
            data[n++]=(uint8_t)((a<<4)|b);
        }
        if(!bb_asset_safe()||!bb_asset_ready())goto reply;
        rc=bb_asset_write(&bb_asset_tx,off,data,n,crc)?0u:3u;goto reply;
    }
    if(con_word(&p,"bacommit")) {
        if(!bb_asset_end(p)||!bb_asset_safe()||!bb_asset_ready())goto reply;
        rc=bb_asset_commit(&bb_asset_tx)?0u:3u;goto reply;
    }
    if(con_word(&p,"bastat")){if(bb_asset_end(p))rc=0;goto reply;}
    if(con_word(&p,"baabort")) {
        if(!bb_asset_end(p))goto reply;
        bb_asset_abort(&bb_asset_tx);rc=0;goto reply; /* Holds remain until a restart. */
    }
    if(con_word(&p,"barestart")) {
        if(!bb_asset_end(p)||!bb_asset_safe()||!bb_asset_ready())goto reply;
        bb_asset_reply(0);if(con_cap){bb_asset_reboot_pending=1;return 1;}fm1_delay_ms(20);fm1_reboot();return 1;
    }
    return 0;
reply:bb_asset_reply(rc);return 1;
}
