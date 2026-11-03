#!/usr/bin/env python3
"""Exercise unmodified firmware list adapter with the real conductor/catalogue.
Only renderer storage and unreachable non-HOME fallback are supplied by the harness.
Usage: python3 welcome_adapter.py OUTPUT [extra compiler arguments]
"""
from pathlib import Path
import json, subprocess, sys, hashlib
src=Path(__file__).resolve().parents[3]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
text=(src/'firmware/src/bb_ui_v3_firmware.inc').read_text()
def function(name):
    import re
    m=re.search(r'^static[^\n]*\b'+name+r'\([^;\n]*\)\s*\{',text,re.M)
    if not m: raise ValueError(name)
    start=m.start();pos=text.index('{',m.start());depth=0
    for i in range(pos,len(text)):
        if text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if depth==0:return text[start:i+1]
    raise ValueError(name)
functions={n:function(n) for n in ['bb_v3_item','bb_v3_window','bb_v3_footer','bb_v3_cat','bb_v3_pattern']}
conductor=out/'conductor_fixture.c'
conductor.write_text((src/'big-beat/composition-20/tests/conductor_test.c').read_text().replace('int main(int argc, char **argv)','int conductor_original_main(int argc, char **argv)'))
head='''#include "CONDUCTOR"
#include "bb_ui_v3/bb_ui.h"
static char bb_v3_items[5][40], bb_v3_items2[BB_UI_LIST-5][40], bb_v3_foot[64];
static uint16_t bb_v3_first[2];
static const bb_player *bb_library_current(void){return &bb_library[0];}
static void bb_v3_pattern_of(bb_ui_in *in,const bb_player *p){(void)in;(void)p;abort();}
'''.replace('CONDUCTOR',str(conductor))
test=r'''
int main(void) {
    out=fopen("/dev/null","wb");CK(out);
    collect();world();bb_nav.page=BB_NAV_STUDIO;bb_nav.bank=BB_NAV_HOME_SONGS;
    unsigned visited=0;
    bb_cd_collection(0);unsigned total=bb_cd_count();CK(total==67);
    for(unsigned v=0;v<total;v++)for(unsigned take=0;take<8;take++) {
        bb_cd_goto(v);bb_cd.take=take;bb_cd.welcome=1;
        bb_ui_in in={0};bb_v3_pattern(&in);
        CK(in.welcome&&in.items<=3&&in.cursor<in.items);
        CK(in.cursor==(v?1u:0u));
        CK(in.sections==sections_of(bb_cd.list.bank_index));
        CK(!strcmp(in.page_name,"CONDUCTOR"));
        for(unsigned k=0;k<in.items;k++) {
            unsigned ix=(v?v-1u:0u)+k;CK(ix<total);
            bb_browser b=bb_cd.list;unsigned bank=bb_cd_bank(ix);
            b.bank_page=bank/BB_BROWSER_BANK_KEYS;
            char expected[256];CK(bb_browser_bank_title(&b,bank%BB_BROWSER_BANK_KEYS,expected,sizeof expected));
            expected[39]=0;for(unsigned j=0;expected[j];j++)if((unsigned char)expected[j]<32u||(unsigned char)expected[j]>126u)expected[j]='?';if(strcmp(expected,in.item[k]))fprintf(stderr,"v=%u k=%u bank=%u expected=[%s] actual=[%s]\n",v,k,bank,expected,in.item[k]);CK(!strcmp(expected,in.item[k]));
        }
        if(v+1u<total)CK(in.cursor+1u<in.items);
        if(v)CK(in.cursor>0);
        CK(strstr(in.footer,"PLAY LOADS ALL 3")&&!strstr(in.footer,"loaded"));
        unsigned commands=audio.commands.count;bb_cd_open_list();memset(&in,0,sizeof in);bb_v3_pattern(&in);
        CK(!in.welcome&&in.cursor<in.items&&audio.commands.count==commands);
        CK(in.tabs==3&&in.tab_cur==0);
        visited++;
    }
    printf("welcome adapter: %u song/legacy-mask cases, %u checks PASS\n",visited,checks);
    return 0;
}
'''
case=out/'welcome_adapter.c';case.write_text(head+'\n'.join(functions.values())+test)
fixture=src/'big-beat/integration/tests/oracle';oracle=out/'oracle.c';oracle.write_text((fixture/'integration-480.c').read_text().replace('@SRC@',str(src)))
args=[x.replace('@SRC@',str(src)).replace('@OUT@',str(out/'test-welcome')).replace('@ORACLE@',str(case)) for x in json.loads((fixture/'compile.json').read_text())]
if sys.platform!='darwin':args=['-Wl,--gc-sections' if a=='-Wl,-dead_strip' else a for a in args]
args+=['-DORACLE_C="'+str(oracle)+'"','-Werror=implicit-function-declaration','-DP3A_BUNDLE_SIZE=1432','-DP3A_HANDOFF_SIZE=504',str(src/'firmware/src/bb_nav.c'),str(src/'firmware/src/bb/bb_factory_recipe.c'),'-I'+str(src/'firmware/src'),'-Dsection(x)=used']
args += sys.argv[2:]
(out/'compile.json').write_text(json.dumps(args,indent=2))
r=subprocess.run(args,capture_output=True,text=True);(out/'BUILD.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stderr[-4000:]);sys.exit(r.returncode)
r=subprocess.run([str(out/'test-welcome')],capture_output=True,text=True);(out/'RUN.log').write_text(r.stdout+r.stderr)
(out/'RESULT.json').write_text(json.dumps({'exit':r.returncode,'functions_sha256':{n:hashlib.sha256(f.encode()).hexdigest()for n,f in functions.items()},'output':r.stdout+r.stderr},indent=2))
print(r.stdout+r.stderr);sys.exit(r.returncode)
