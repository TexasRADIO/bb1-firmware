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
import bisect, hashlib, json, re, subprocess, sys
from pathlib import Path

SRC, CMDS, OUT, TOOLS = Path('/src'), Path('/cmds'), Path('/out'), Path('/tools')
recipe = json.loads(Path('/recipe/recipe.json').read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
report = {'units': {}, 'source': str(SRC), 'recipe_sha256': sha('/recipe/recipe.json'), 'installed': False,
          'packaged': False, 'hardware_tested': False}

def rewrite(args, unit=None):
    out = []
    for a in args:
        a = a.replace('/work/source', str(SRC)).replace('/work/group_target_build-r1', str(OUT))
        out.append(a)
    for old, new in recipe.get('flag_edits', {}).items():   # explicit, recorded flag changes only
        out = [new if a == old else a for a in out if not (new is None and a == old)]
    out += recipe.get('extra_flags', [])
    for old, new in recipe.get('unit_flag_edits', {}).get(unit, {}).items():   # recorded per-unit changes (2.0 size lane)
        assert out.count(old) == 1, (unit, old)
        out[out.index(old)] = new
    return out

def validate_profile(configuration, command_arguments):
    """Validate preprocessing flags for every unit sharing the audited profile.

    Low-level startup/probe units may use a separate ABI and must be excluded
    explicitly by the recipe; missing flags are never assumed to mean zero.
    """
    required = dict(configuration.get('required_disabled_flags', {}))
    for key, value in configuration.get('required_profile_flags', {}).items():
        if key in required and required[key] != value:
            raise ValueError('conflicting profile requirement: ' + key)
        required[key] = value
    units = configuration.get('profile_guard_units', ['main', 'bb_audio'])
    if not units or len(units) != len(set(units)) or any(unit not in configuration['link_order'] for unit in units):
        raise ValueError('invalid profile guard unit list')
    if 'profile_guard_units' in configuration:
        excluded = set(configuration.get('profile_guard_exclusions', {}))
        if excluded.intersection(units) or excluded.union(units) != set(configuration['link_order']):
            raise ValueError('every unit needs a profile guard or explicit exclusion')
    for unit in units:
        definitions = {}
        for arg in command_arguments[unit]:
            if arg.startswith('-D'):
                key, _, value = arg[2:].partition('=')
                definitions[key] = value or '1'
            elif arg.startswith('-U'):
                definitions.pop(arg[2:], None)
        for key, value in required.items():
            if definitions.get(key) != value:
                raise ValueError('profile requires provider/ABI review: %s %s=%r, expected %r' %
                                 (unit, key, definitions.get(key), value))

# Refuse an incompatible profile before starting even the first compilation.
if recipe.get('required_disabled_flags') or recipe.get('required_profile_flags'):
    profile_commands = {}
    for unit in recipe.get('profile_guard_units', ['main', 'bb_audio']):
        command = 'main-front-command.json' if unit == 'main' else unit + '-o-command.json'
        profile_commands[unit] = rewrite(json.loads((CMDS / command).read_text()), unit)
    validate_profile(recipe, profile_commands)


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
    args = rewrite(cmd, name)
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
# recipe-recorded additions (2.0 size lane): functions defined in the main-loop UI, navigation, help, session and
# project files listed in size_include also get minsize. Each definition's file comes from the line markers of main
# preprocessed with the same command (so macro-renamed and attributed definitions count). A function stays -Os if
# main's unoptimised IR reaches it from an interrupt body or from a further timing root (CPU1 worker, fault handler);
# reach across units is checked offline (claude/2.0/size on main).
inc = recipe.get('size_include')
if inc:
    pre = [a for a in front if a not in ('-S', '-emit-llvm', '-MMD')]
    pre = [a for i, a in enumerate(pre) if not (a == '-MF' or (i and pre[i - 1] == '-MF'))]
    pre[pre.index('-o') + 1] = str(OUT / 'main.i'); pre.insert(pre.index('-o'), '-E')
    run('pi32v2/bin/clang', pre, 'main-pre')
    text, defined = (OUT / 'main.i').read_text(errors='replace'), set(m.group(2) for m in ns['IR_DEF'].finditer(raw.read_text()))
    marks = [(m.start(), m.group(1).split('firmware/src/')[-1]) for m in re.finditer(r'^# \d+ "([^"]+)"', text, re.M)]
    at = [p for p, _ in marks]
    files, where = set(inc['files']), {}
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', text):
        name = m.group(1)
        if name not in defined or name in where:
            continue
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {'(': 1, ')': -1}.get(text[i], 0); i += 1
            if depth == 0: break
        if re.match(r'\s*(__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)\s*)*\{', text[i:i + 300]):
            where[name] = marks[bisect.bisect_right(at, m.start()) - 1][1]
    wanted = set(n for n, f in where.items() if f in files)
    missing = sorted(files - set(where.values()))
    assert not missing, f'size_include files define nothing in main: {missing}'
    ns['ISR_ROOTS'] = tuple(ns['ISR_ROOTS']) + tuple(inc['timing_roots'])
    timing = ns['isr_reach'](raw.read_text())
    ir, hit = sized.read_text(), set()
    def add_minsize(m):
        if m.group(2) not in wanted or m.group(2) in timing or ' minsize' in m.group(1):
            return m.group(0)
        hit.add(m.group(2)); return m.group(1) + ' minsize' + m.group(3)
    ir = ns['IR_DEF'].sub(add_minsize, ir)
    sized.write_text(ir)
    kept = sorted(wanted & timing)
    report['size_include'] = {'files': len(files), 'functions': sorted(hit), 'timing_kept': kept}
    print(f"size_include: {len(hit)} more main-loop functions built for size from {len(files)} files "
          f"({len(kept)} reached from a timing root kept -Os)")
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
