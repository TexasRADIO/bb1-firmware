#!/usr/bin/env python3
"""From-source FM-1 target build for Big Beat. Runs INSIDE the pinned Jieli container.

Every object is compiled from the mounted source tree; no object is reused from an earlier build.
Compiler arguments come from a recorded command set (one JSON argument list per unit, as written
by the project's group_target_build driver) with only path prefixes rewritten, so flags stay exact.

Mounts (all read-only except /out):
  /src     source tree (firmware/, build/gen/)      /cmds   recorded *-command.json + report.json
  /recipe  recipe.json (roots, link order, flag edits)  /tools  size_fns.py, mark_ui.py
  /link    linker overlay (run-r3/guarded-gc.ld)   /assets  909 shared assets (asset-symbols.ld)
  /opt/jieli  toolchain                            /out    build output
Never packages, installs or contacts hardware.
"""
import hashlib, json, re, subprocess, sys
from pathlib import Path

SRC, CMDS, OUT, TOOLS = Path('/src'), Path('/cmds'), Path('/out'), Path('/tools')
recipe = json.loads(Path('/recipe/recipe.json').read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
report = {'units': {}, 'source': str(SRC), 'recipe_sha256': sha('/recipe/recipe.json'), 'installed': False,
          'packaged': False, 'hardware_tested': False}

def rewrite(args):
    out = []
    for a in args:
        a = a.replace('/work/source', str(SRC)).replace('/work/group_target_build-r1', str(OUT))
        out.append(a)
    for old, new in recipe.get('flag_edits', {}).items():   # explicit, recorded flag changes only
        out = [new if a == old else a for a in out if not (new is None and a == old)]
    out += recipe.get('extra_flags', [])
    return out

def run(tool, args, label):
    (OUT / f'{label}-command.json').write_text(json.dumps([str(a) for a in args], indent=1) + '\n')
    p = subprocess.run(['/opt/jieli/' + tool, *map(str, args)], cwd=SRC, capture_output=True, text=True, timeout=300)
    (OUT / f'{label}.log').write_text(p.stdout + p.stderr)
    if p.returncode:
        raise SystemExit(f'{label} failed:\n{(p.stdout + p.stderr)[-4000:]}')
    return p

order = recipe['link_order']
for name in order:
    if name == 'main': continue
    cmd = json.loads((CMDS / f'{name}-o-command.json').read_text())
    assert cmd[0] == '-target', name
    args = rewrite(cmd)
    run('pi32v2/bin/clang', args, f'{name}-o')
    src = next((a for a in args if a.startswith(str(SRC)) and a.endswith(('.c', '.S'))), None)
    report['units'][name] = {'source': src, 'source_sha256': sha(src) if src else None, 'object_sha256': sha(OUT / f'{name}.o')}

# main: unoptimised IR -> size attributes on main-loop UI helpers -> codegen (recorded pipeline)
front = rewrite(json.loads((CMDS / 'main-front-command.json').read_text()))
run('pi32v2/bin/clang', front, 'main-front')
raw, sized = OUT / 'main.ll', OUT / 'main-size.ll'
ns = {'__file__': str(SRC / 'tools/size_fns.py'), '__name__': 'size_fns'}   # its root = parents[1] = the source tree
exec(compile((TOOLS / 'size_fns.py').read_text(), 'size_fns', 'exec'), ns)
ns['main'](raw, sized)
marker = (TOOLS / 'mark_ui.py').read_text()
marker = marker.replace("Path('/work/target/tools/size_fns.py')", f"Path('{TOOLS}/size_fns.py')").replace('/work/target/', f'{SRC}/')
(OUT / 'mark_ui.py').write_text(marker)
m = subprocess.run([sys.executable, str(OUT / 'mark_ui.py'), str(sized)], capture_output=True, text=True, timeout=60)
(OUT / 'mark-ui.log').write_text(m.stdout + m.stderr); m.check_returncode()
# recipe-recorded exceptions: functions the reproduced build left at -Os (boot42: cv_line, cv_line_t, cv_text_fit)
excl = recipe.get('size_exclude', [])
if excl:
    ir = sized.read_text(); n = 0
    for name in excl:
        ir, k = re.subn(r'^(define [^\n]*@' + re.escape(name) + r'\([^\n]*\)) minsize ', r'\1 ', ir, flags=re.M); n += k
    assert n == len(excl), f'size_exclude matched {n} of {len(excl)}'
    sized.write_text(ir)
report['size_exclude'] = excl
run('pi32v2/bin/clang', rewrite(json.loads((CMDS / 'main-o-command.json').read_text())), 'main-o')
report['units']['main'] = {'source': str(SRC / 'firmware/src/felucca.c'), 'source_sha256': sha(SRC / 'firmware/src/felucca.c'),
                           'object_sha256': sha(OUT / 'main.o')}

# link (same procedure as the project's retained linker)
dest = OUT / 'candidate'; dest.mkdir()
flags = ['--gc-sections', '-T', recipe['linker_script'], '-T', recipe['asset_symbols']]
for root in recipe['roots']: flags += ['-u', root]
objs = [OUT / f'{n}.o' for n in order]
run('pi32v2/bin/ld', [*flags, *objs, '-o', dest / 'app.elf'], 'link')
symbols = subprocess.run(['/opt/jieli/common/bin/objdump', '-t', dest / 'app.elf'], capture_output=True, text=True, check=True).stdout
(dest / 'symbols').write_text(symbols)
addr = lambda n: int(re.search(r'^([0-9a-f]+).*\s' + re.escape(n) + r'$', symbols, re.M)[1], 16)
blob = bytearray()
for section, sym in [('text', '_start'), ('ram_text', '_rt_load'), ('data', '_data_load')]:
    path = dest / f'{section}.bin'
    subprocess.run(['/opt/jieli/common/bin/objcopy', '-O', 'binary', '-j', '.' + section, dest / 'app.elf', path], check=True)
    at = addr(sym) - 0x02000120; assert at >= len(blob), (section, at, len(blob))
    blob += b'\xff' * (at - len(blob)) + path.read_bytes()
blob += b'\xff' * (-len(blob) % 4)
(dest / 'app.bin').write_bytes(blob)
report['candidate'] = {'sha256': hashlib.sha256(blob).hexdigest(), 'bytes': len(blob), 'code_free': 581564 - len(blob),
                       'ram_free': 0x1c1a000 - addr('_bss_end'), 'pool_free': 0x1c74000 - addr('_pool_end'),
                       'ram_text_bytes': (dest / 'ram_text.bin').stat().st_size}
report['resource_limits_pass'] = (report['candidate']['code_free'] >= 0 and report['candidate']['ram_free'] >= 4096
                                  and report['candidate']['pool_free'] >= 8192)
(OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
print(json.dumps(report['candidate'], indent=1))
