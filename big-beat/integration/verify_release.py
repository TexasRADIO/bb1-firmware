#!/usr/bin/env python3
"""One command for a release candidate: source inventory, host tests, worker/serial oracle, 480-section
level sweep, from-source target build and fit, deterministic package with negatives, then a summary
of hashes. Offline; never contacts an FM-1. Everything it proves is host or build evidence.

usage: python3 -I verify_release.py SOURCE OUT [--expect-tree SHA] [--expect-app SHA] [--expect-package SHA]
                                   [--host-only] [--recipe NAME]
  --recipe NAME  build/recipes/NAME (default candidate); a measurement recipe is recorded in summary.json
  SOURCE  firmware source tree (candidate-src checkout)
  OUT     new directory for logs, binaries and summary.json (must not exist)

Pinned inputs outside Git (toolchain, linker overlay, asset symbols, installed sound-library read-back,
packaging dependencies) are checked by SHA-256 against inputs.json before use; a missing or changed input
fails the run. Exit 0 only when every step passes."""
from pathlib import Path
import hashlib, json, os, re, shlex, subprocess, sys, time

HERE = Path(__file__).resolve().parent
args = sys.argv[1:]
def opt(name):
    if name in args:
        i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
expect_tree, expect_app, expect_pkg = opt('--expect-tree'), opt('--expect-app'), opt('--expect-package')
recipe_name = opt('--recipe') or 'candidate'
assert re.fullmatch(r'[\w.-]+', recipe_name) and (HERE / 'build/recipes' / recipe_name / 'recipe.json').is_file(), recipe_name
host_only = '--host-only' in args
if host_only: args.remove('--host-only')
SRC, OUT = Path(args[0]).resolve(), Path(args[1]).resolve()
assert not OUT.exists(), f'{OUT} exists'
OUT.mkdir(parents=True)
T = HERE / 'tests'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
summary = {'hardware_verified': False, 'device_contacted': False,
           'note': 'Host and build evidence only. Timing, sound, button routes and recovery need the FM-1.',
           'source': str(SRC), 'recipe': recipe_name, 'started': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'steps': {}}
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

def finish(ok):
    summary['passed'] = ok
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: summary[k] for k in summary if k != 'steps'} | {'steps': {k: v.get('ok') for k, v in summary['steps'].items()}}, indent=2))
    sys.exit(0 if ok else 1)

def run(cmd, log, cwd=SRC, shell=False):
    r = subprocess.run(cmd, cwd=cwd, env=ENV, capture_output=True, text=True, shell=shell)
    (OUT / log).write_text((cmd if shell else ' '.join(map(str, cmd))) + '\n--- stdout\n' + r.stdout + '\n--- stderr\n' + r.stderr)
    return r

def step(name, fn):
    print(f'== {name}', flush=True)
    try:
        res = fn() or {}
        res['ok'] = True
    except Exception as e:                                     # record and stop: fail closed
        res = {'ok': False, 'error': f'{type(e).__name__}: {e}'}
    summary['steps'][name] = res
    if not res['ok']:
        print('   FAILED:', res['error']); finish(False)

# 1. pinned inputs ---------------------------------------------------------------------------------
inputs = json.loads((HERE / 'inputs.json').read_text())
def check_inputs():
    bad = {}
    for name, e in inputs['files'].items():
        p = Path(e['path'])
        if not p.is_file(): bad[name] = 'missing'
        elif sha(p) != e['sha256']: bad[name] = 'changed: ' + sha(p)
    for name, e in inputs['trees'].items():
        root = Path(e['path']); lines = []
        for d, _, fs in os.walk(root):
            lines += [f'{(Path(d) / f).relative_to(root)} {sha(Path(d) / f)}' for f in fs]
        h = hashlib.sha256('\n'.join(sorted(lines)).encode()).hexdigest()
        if h != e['sha256']: bad[name] = f'tree changed: {h} ({len(lines)} files)'
    if not host_only:
        img = inputs['docker_image']
        r = subprocess.run([os.environ.get('DOCKER', '/Users/loudounhq/.docker/bin/docker'), 'image', 'inspect', img['tag'], '--format', '{{.Id}}'],
                           capture_output=True, text=True)
        if r.stdout.strip() != img['id']: bad['docker_image'] = r.stdout.strip() or r.stderr.strip()[:200]
    assert not bad, bad
    return {'files': len(inputs['files']), 'trees': len(inputs['trees'])}
step('inputs', check_inputs)

# 2. source inventory (same method as RELEASE-RC1: "path sha256" per build/ and firmware/ file, sorted)
def inventory():
    manifest = SRC / 'SOURCE-BUNDLE-MANIFEST.json'
    in_git = subprocess.run(['git', 'rev-parse', '--is-inside-work-tree'], cwd=SRC, capture_output=True, text=True).returncode == 0
    if in_git:
        files = subprocess.run(['git', 'ls-files'], cwd=SRC, capture_output=True, text=True, check=True).stdout.split('\n')
        dirty = subprocess.run(['git', 'status', '--porcelain', '--', 'build', 'firmware'], cwd=SRC, capture_output=True, text=True).stdout
    else:
        # An extracted GPL source bundle has no Git: list its files and hold them to the bundle's own manifest.
        assert manifest.is_file(), 'not a Git checkout and no SOURCE-BUNDLE-MANIFEST.json'
        files = [str(p.relative_to(SRC)) for d in ('build', 'firmware') for p in (SRC / d).rglob('*') if p.is_file()]
        dirty = ''
    files = sorted(f for f in files if f.startswith(('build/', 'firmware/')))
    lines = [f'{f} {sha(SRC / f)}' for f in files]
    (OUT / 'source-inventory.txt').write_text('\n'.join(lines) + '\n')
    tree = hashlib.sha256('\n'.join(lines).encode()).hexdigest()
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=SRC, capture_output=True, text=True).stdout.strip()
    if expect_tree: assert tree == expect_tree, f'tree {tree} != {expect_tree}'
    if not in_git:
        want = json.loads(manifest.read_text())['candidate_tree_sha256']
        assert tree == want, f'bundle tree {tree} != its manifest {want}'
    return {'tree_sha256': tree, 'files': len(lines), 'git_head': head if in_git else None, 'source_bundle': not in_git,
            'uncommitted_changes': bool(dirty.strip())}
step('inventory', inventory)

# 3. host tests (sanitizers on) ------------------------------------------------------------------------
front = json.loads((HERE / 'build/cmds-r4-onepass/main-front-command.json').read_text())
FRONT_D = [a for a in front if a.startswith('-D') and not a.startswith('-D__DATE__')]
flags = (T / 'host_flags.sh').read_text()
HOST = {k: shlex.split(re.search(rf'^{k}="(.*)"$', flags, re.M).group(1)) for k in ('HOST_DEFS', 'HOST_INC', 'HOST_WARN')}
SAN = ['-std=c11', '-O1', '-Wall', '-Wextra', '-fsanitize=address,undefined']
B = OUT / 'bin'; B.mkdir()

def compile_and_run(name, cc, runargs=(), expect=None, returncode=0):
    r = run(['clang', *cc, '-o', str(B / name)], f'{name}-compile.log')
    assert r.returncode == 0, f'{name} compile failed (see {name}-compile.log)'
    r = run([str(B / name), *runargs], f'{name}.log', cwd=OUT)
    assert r.returncode == returncode, f'{name} exit {r.returncode} (expected {returncode})'
    last = (r.stdout.strip().splitlines() or [''])[-1]
    if expect: assert re.search(expect, r.stdout), f'{name}: {last}'
    return last

def host_tests():
    res = {}
    res['routes'] = compile_and_run('test_routes', [*SAN, '-Werror', '-I', 'firmware/src', '-I', 'firmware/src/bb_ui_v3',
                                    str(T / 'test_routes.c'), 'firmware/src/bb_nav.c'], [str(OUT / 'route-matrix.csv')],
                                    r'legacy normal pages 0; \d+ checks, 0 failures')
    res['captures'] = compile_and_run('capture', [*SAN, '-Wno-unused-function', '-Wno-missing-field-initializers', '-Dsection(x)=used',
                                      '-I', 'firmware/src', '-I', 'build/gen', str(T / 'capture.c'), 'firmware/src/bb_nav.c'],
                                      [str(OUT / 'captures.raw')], r'0 lint/route failures')
    res['kit_intent'] = compile_and_run('test_kit_intent', [*SAN, '-Werror', '-Wno-unused-function', *FRONT_D, *HOST['HOST_INC'], str(T / 'test_kit_intent.c')])
    for t in ('test_sound_pending', 'test_sound_picker_outcomes'):
        res[t] = compile_and_run(t, [*HOST['HOST_WARN'], '-Wno-unused-parameter', *HOST['HOST_DEFS'], *HOST['HOST_INC'],
                                     str(T / f'{t}.c'), str(T / 'stub_telegram_bank.c'), 'firmware/src/bb/bb_sound_picker.c'])
    # an off drum layer switches on only when its confirmed choice is applied (bb_nav_arm.h; Duke #4 6469741984)
    res['nav_arm'] = compile_and_run('test_nav_arm', [*SAN, '-Werror', '-I', 'firmware/src', str(T / 'test_nav_arm.c')],
                                     expect=r'checks, 0 failures')
    # a drum lane names a sounding sampled kit as the KIT page does (bb_ui_kit_label.h; #4 6471457725)
    res['kit_label'] = compile_and_run('test_kit_label', [*SAN, '-Werror', '-I', 'firmware/src', str(T / 'test_kit_label.c')],
                                       expect=r'checks, 0 failures')
    # firmware libc.c memset/memcpy against reference byte loops (the oracle uses the host libc, so this is the only check)
    res['libc'] = compile_and_run('libc_equiv', [*SAN, '-fno-builtin', '-Wno-unused-function', f'-DLIBC_C="{SRC}/firmware/src/libc.c"',
                                  str(T / 'libc/libc_equiv.c')], expect=r'cases, 0 mismatches')
    # 1.2 scene conversion (r9.14 step 2): 1.1-library scenes load under a successor library, everything else refused
    if 'bb_sample_scene_set_predecessor' in (SRC / 'firmware/src/bb/sample_scene.c').read_text():
        res['scene_convert'] = compile_and_run('scene_convert', [*SAN, *HOST['HOST_WARN'], '-Wno-unused-parameter', '-Dsection(x)=used',
            *HOST['HOST_DEFS'], *HOST['HOST_INC'], str(T / 'scene_convert/scene_convert.c'),
            *[f'firmware/src/bb/{f}.c' for f in ('sample_scene', 'group_scene', 'bb_family_scene', 'bb_session', 'bb_core',
              'bb909_patch', 'bb_crc32', 'bb_wire', 'group_gain', 'bb909_controls')], '-lm'],
            expect=r'scene conversion: \d+ checks, 0 failures')
    # 1.5 ACID bank: Dillo reads every ACID phrase, the drum reader refuses them, ERAS/SONGS unchanged
    if (SRC / 'firmware/src/bb/bb_browser.c').read_text().count('"acid"'):
        res['acid_bank'] = compile_and_run('acid_bank', [*SAN, *HOST['HOST_WARN'], '-Wno-unused-parameter', '-Dsection(x)=used',
            *HOST['HOST_DEFS'], *HOST['HOST_INC'], str(T / 'acid_bank/acid_bank.c'),
            *[f'firmware/src/bb/{f}.c' for f in ('bb_factory', 'bb_factory_recipe', 'bb_browser', 'bb_catalogue', 'bb_paged_container',
              'bb_paged_records', 'bb_catalogue_page', 'bb_crc32', 'bb_core', 'bb_factory_pattern', 'bb_factory_drum',
              'bb_note_expression_catalogue', 'bb_note_expression')], '-lm'],
            expect=r'acid bank: \d+ banks, \d+ phrases, \d+ checks, 0 failures')
    # 1.5 song tempo: every song bank reads its sheet tempo/swing through the player's browser walk
    if 'bb_factory_read_song_tempo' in (SRC / 'firmware/src/bb/bb_factory_recipe.c').read_text():
        res['song_tempo'] = compile_and_run('song_tempo', [*SAN, *HOST['HOST_WARN'], '-Wno-unused-parameter', '-Dsection(x)=used',
            *HOST['HOST_DEFS'], *HOST['HOST_INC'], str(T / 'song_tempo/song_tempo.c'),
            *[f'firmware/src/bb/{f}.c' for f in ('bb_factory', 'bb_factory_recipe', 'bb_browser', 'bb_catalogue', 'bb_paged_container',
              'bb_paged_records', 'bb_catalogue_page', 'bb_crc32', 'bb_core')], '-lm'],
            expect=r'song tempo: \d+ songs, \d+ checks, 0 failures')
    # L3 fix A gate (fix A trees only, those with x0x/metal_capture.inc): kit A's metal lanes via the compact capture,
    # merged per sample in CB->CY order, are bit-identical to the released full render (outputs, whole kit, groups);
    # the pre-sum and state mutations must fail (#4 6483954898, 6485319810, 6485803291, 6488119452)
    if (SRC / 'firmware/src/bb/x0x/metal_capture.inc').exists():
        cap = [*HOST['HOST_WARN'], '-Wno-unused-parameter', '-Wno-unused-variable', '-Dsection(x)=used', *HOST['HOST_DEFS'],
               *HOST['HOST_INC'], str(T / 'fixA/capture_exact.c'), 'firmware/src/bb/group_gain.c', '-lm']
        res['fixA_capture'] = compile_and_run('capture_exact', cap, expect=r'capture gate: \d+ checks, \d+ spans with metal, \d+ parameter/fader changes, 0 failures')
        res['fixA_presum_fails'] = compile_and_run('capture_exact_presum', ['-DMUTATE_PRESUM', *cap], expect=r' [1-9][0-9]* failures', returncode=1)
        res['fixA_state_fails'] = compile_and_run('capture_exact_state', ['-DMUTATE_STATE', *cap], expect=r' [1-9][0-9]* failures', returncode=1)
        trg = [*cap[:-3], str(T / 'fixA/trigger_exact.c'), 'firmware/src/bb/group_gain.c', '-lm']
        res['fixA_trigger'] = compile_and_run('trigger_exact', trg, expect=r'metal trigger: \d+ checks, 0 refused, 0 failures')
        res['fixA_trigger_mutation_fails'] = compile_and_run('trigger_exact_mut', ['-DMUTATE_TRIGGER', *trg], expect=r' [1-9][0-9]* failures', returncode=1)
        # a whole CPU1 call: spans with kit A's hits anywhere in it, the 12-hit cap refused unchanged (bb_metal_capture_call)
        cal = [*cap[:-3], str(T / 'fixA/call_exact.c'), 'firmware/src/bb/group_gain.c', '-lm']
        if 'bb_metal_capture_call' in (SRC / 'firmware/src/bb/x0x/metal_capture.inc').read_text():
            res['fixA_call'] = compile_and_run('call_exact', cal, expect=r'call gate: \d+ calls compared .*, 0 failures')
            for m in ('SEGFLAGS', 'HITSHIFT', 'EQUALORDER'):
                res[f'fixA_call_{m.lower()}_fails'] = compile_and_run(f'call_exact_{m.lower()}', [f'-DMUTATE_{m}', *cal], expect=r' [1-9][0-9]* failures', returncode=1)
        # the core-1 job container gives the direct result and refuses tampering (hal/bb_c1_metal_job.h)
        if (SRC / 'firmware/hal/bb_c1_metal_job.h').exists():
            res['fixA_job'] = compile_and_run('metal_job', [*cap[:-3], '-Wno-unused-function', str(T / 'fixA/metal_job.c'), 'firmware/src/bb/group_gain.c', '-lm'],
                                              expect=r'fix A job: \d+ calls, \d+ tamper rounds, \d+ B job, \d+ checks, 0 failures')
    if (OUT / 'route-matrix.csv').read_bytes() != (HERE / 'route-matrix.csv').read_bytes():
        res['route_matrix'] = 'DIFFERS from the checked-in route-matrix.csv'
    else:
        res['route_matrix'] = 'identical to route-matrix.csv'
    return res
step('host-tests', host_tests)

# 4. worker vs serial oracle (Duke's integration-480, re-rooted) ---------------------------------------
def oracle():
    o = OUT / 'oracle'; o.mkdir()
    (o / 'integration-480.c').write_text((T / 'oracle/integration-480.c').read_text().replace('@SRC@', str(SRC)))
    base = [a.replace('@SRC@', str(SRC)).replace('@ORACLE@', str(o / 'integration-480.c')) for a in json.loads((T / 'oracle/compile.json').read_text())]
    res = {}
    for name, drop, add in (('worker', (), ()), ('serial', (), ('-DFORCE_SERIAL=1',)), ('faults', ('-DAUDIO_ONLY=1',), ())):
        c = [a.replace('@OUT@', str(o / name)) for a in base if a not in drop] + list(add)
        r = run(c, f'oracle/{name}-compile.log'); assert r.returncode == 0, f'oracle {name} compile'
        r = run([str(o / name)], f'oracle/{name}.log', cwd=o); assert r.returncode == 0, f'oracle {name} exit {r.returncode}'
        tail = r.stdout.strip().splitlines()[-2:]
        stats = dict(kv.split('=') for kv in tail[1].split())
        res[name] = {'digest': tail[0].split()[0], **{k: stats[k] for k in ('worker_jobs', 'refused', 'failed_calls', 'retirements')}}
    assert res['worker']['digest'] == res['serial']['digest'], 'worker and serial digests differ'
    assert int(res['worker']['worker_jobs']) > 0 and int(res['serial']['worker_jobs']) == 0
    assert res['faults']['failed_calls'] == res['faults']['retirements'] and res['faults']['refused'] == '0'
    return res
step('oracle', oracle)

# 5. 480-section level sweep at the default gain ------------------------------------------------------
def sweep():
    r = run(['python3', '-I', str(T / 'sweep/build_sweep.py'), str(SRC), str(B / 'sweep')], 'sweep-compile.log')
    assert r.returncode == 0, 'sweep compile'
    r = run([str(B / 'sweep'), inputs['files']['sound_library_readback']['path'], 'default', '800'], 'sweep.log', cwd=OUT)
    assert r.returncode == 0, f'sweep exit {r.returncode}'
    (OUT / 'sweep.csv').write_text(r.stdout)
    import csv, io
    rows = list(csv.DictReader(io.StringIO(r.stdout)))
    tot = {k: sum(float(x[k] or 0) for x in rows) for k in ('limited_frames', 'invalid_faults', 'refused', 'wrong_kit')}
    assert rows and not any(tot.values()), tot
    return {'cases': len(rows), **tot, 'worst_peak_dbfs': max(float(x['peak_dbfs']) for x in rows)}
step('sweep', sweep)

if host_only:
    summary['steps']['target'] = {'ok': None, 'skipped': '--host-only'}
    finish(True)

# 6. from-source target build, fit -------------------------------------------------------------------
def target():
    bdir = OUT / 'build'
    r = run(['bash', str(HERE / 'build/run_build.sh'), str(SRC), str(HERE / 'build/cmds-r4-onepass'), str(HERE / 'build/recipes' / recipe_name), str(bdir)],
            'target-build.log')
    assert r.returncode == 0, 'target build failed (see target-build.log)'
    rep = json.loads((bdir / 'report.json').read_text())
    link = rep['candidate']
    assert rep['resource_limits_pass'], 'resource limits'
    if expect_app: assert link['sha256'] == expect_app, f"app {link['sha256']} != {expect_app}"
    return {k: link[k] for k in ('sha256', 'bytes', 'code_free', 'ram_free', 'pool_free', 'ram_text_bytes')}
step('target', target)

# 7. deterministic package, eight negatives --------------------------------------------------------
def package():
    r = run(['python3', str(HERE / 'package/package_candidate.py'), str(OUT / 'build'), str(OUT / 'package'), 'candidate'], 'package.log')
    assert r.returncode == 0, 'packaging failed (see package.log)'
    rep = json.loads((OUT / 'package/candidate-package-report.json').read_text())
    assert len(rep['negative_cases']) == 8 and rep['installed'] is False
    if expect_pkg: assert rep['package']['sha256'] == expect_pkg, f"package {rep['package']['sha256']} != {expect_pkg}"
    return {'sha256': rep['package']['sha256'], 'bytes': rep['package']['bytes'], 'negative_cases': len(rep['negative_cases']),
            'rollback_sha256': rep['rollback']['sha256']}
step('package', package)
finish(True)
