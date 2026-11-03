#!/usr/bin/env python3
"""Turn the preprocessed firmware unity build (felucca.c -> .i) into a host translation unit:
  1. the pi32v2 inline assembly (csync, ssync, cli, sti, icfg, flushinv, the power-settle delay loop)
     becomes an empty asm statement (a compiler barrier); `%0 = icfg` reads 0;
  2. the panel's SPI calls (fm1_lcd_send_cmd / fm1_lcd_send_data / fm1_lcd_wait) call the host's
     ST7789 emulator (host_os.c) instead of writing SPI registers;
  3. the host harness (harness.c) is appended, so it sees the firmware's static functions and state.
Everything else is the firmware's own code, unchanged. Each rewrite must match exactly the expected
number of times, so a source change cannot slip past silently.
usage: host_prep.py <in.i> <harness.c> <out.i>
       host_prep.py <in.i> - <out.i>      (asm only: for the Thumb-2 size estimate, tools/size_delta.sh)"""
import re, sys
src, harness, out = sys.argv[1:4]
s = open(src).read()

def sub(pattern, repl, text, count, flags=0):
    new, n = re.subn(pattern, repl, text, flags=flags)
    assert n == count, (pattern, n, count)
    return new

s = sub(r'__asm__ volatile\("%0 = icfg" : "=r"\(v\)\);', 'v=0;', s, 1)
s = sub(r'__asm__ volatile\("(csync|ssync|cli|csync\\n\\tsti|icfg = %0|flushinv \[%0\])"', '__asm__ volatile(""', s,
        len(re.findall(r'__asm__ volatile\("(csync|ssync|cli|csync\\n\\tsti|icfg = %0|flushinv \[%0\])"', s)))
s = sub(r'__asm__ volatile\("goto 2f\\n1:\\n\\tnop\\n\\t%0 \+= 1\\n2:\\n\\tif \(%0 != 0\) goto 1b"', '__asm__ volatile(""', s, 1)
left = [m for m in re.findall(r'__asm__ volatile\("([^"]*)"', s) if m]
assert not left, left

def body(name, new_body):
    global s
    m = re.search(r'\b' + name + r'\((?:[^)]*)\)\s*\{', s)
    assert m, name
    i = m.end(); depth = 1
    while depth:
        depth += {'{': 1, '}': -1}.get(s[i], 0)
        i += 1
    s = s[:m.end()] + new_body + '}' + s[i:]

if harness == '-':
    open(out, 'w').write(s)
    sys.exit(0)
proto = 'void host_panel_cmd(unsigned c); void host_panel_data(const void *p, unsigned n);\n'
body('fm1_lcd_wait', ' return; ')
body('fm1_lcd_send_cmd', ' host_panel_cmd(c); ')
body('fm1_lcd_send_data', ' host_panel_data(p, n); ')
open(out, 'w').write(proto + s + '\n#line 1 "harness.c"\n' + open(harness).read())

# Explicit post-reset failure injection for a separate host diagnostic artifact.
# Production emu.c and all native builds never contain this check.
if harness.endswith('emu-diagnostic.c'):
    import hashlib,json
    text=open(out).read()
    needle='if(!bb_audio_init(a,enter,leave,context))return BB_SINGLE_HELD_FAILURE;'
    assert text.count(needle)==1, ('post-reset hook count',text.count(needle))
    replacement=needle+'if(emu_project_forced_reset_failure)return BB_SINGLE_HELD_FAILURE;'
    text='static unsigned emu_project_forced_reset_failure;\n'+text.replace(needle,replacement)
    open(out,'w').write(text)
    open(out+'.instrumentation.json','w').write(json.dumps({'test_only':True,'hook':'return HELD_FAILURE immediately after actual bb_audio_init','replacements':1,'preprocessed_sha256':hashlib.sha256(text.encode()).hexdigest(),'device_contact':False},indent=2)+'\n')
