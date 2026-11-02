#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BB1 2.0 corresponding source: an exact `git archive` of one commit, checked and hash-recorded.

The 2.0 tree carries its own build recipe, tests, catalogue and licences, so the source bundle is the commit itself.
With --build it is only accepted when a clean native build of the EXTRACTED archive reproduces the given app
byte for byte (the same pinned container, recipe and command set as the release build).

usage: bundle_source_20.py COMMIT OUT_DIR [--expect-app SHA256] [--build]
writes OUT_DIR/bb1-source-2.0.tar and OUT_DIR/SOURCE-BUNDLE.json; never touches the working tree."""
import hashlib, io, json, os, subprocess, sys, tarfile
from pathlib import Path

args = sys.argv[1:]
build = '--build' in args
if build: args.remove('--build')
expect = None
if '--expect-app' in args:
    i = args.index('--expect-app'); expect = args[i + 1].lower(); del args[i:i + 2]
if len(args) != 2: raise SystemExit(__doc__)
commit, out = args[0], Path(args[1]).resolve()
repo = Path(__file__).resolve().parents[3]
full = subprocess.run(['git', 'rev-parse', '--verify', commit + '^{commit}'], cwd=repo, capture_output=True, text=True, check=True).stdout.strip()
out.mkdir(parents=True, exist_ok=False)
prefix = 'bb1-2.0-source/'
tar = out / 'bb1-source-2.0.tar'
with tar.open('wb') as f:
    subprocess.run(['git', 'archive', '--format=tar', '--prefix=' + prefix, full], cwd=repo, stdout=f, check=True)
data = tar.read_bytes()
digest = hashlib.sha256(data).hexdigest()

# contents: every tracked file, the licences, the recipe; nothing generated
with tarfile.open(fileobj=io.BytesIO(data)) as t:
    names = [m.name[len(prefix):] for m in t.getmembers() if m.isfile()]
tracked = subprocess.run(['git', 'ls-tree', '-r', '--name-only', full], cwd=repo, capture_output=True, text=True, check=True).stdout.split('\n')
tracked = [n for n in tracked if n]
assert sorted(names) == sorted(tracked), 'archive does not equal the commit tree'
for need in ('LICENSE', 'LICENSING.md', 'LICENSES/Apache-2.0.txt', 'firmware/src/felucca.c',
             'big-beat/integration/build/run_build.sh', 'big-beat/integration/build/recipes/composition-20-lean/recipe.json'):
    assert need in names, f'source bundle is missing {need}'
binaries = [n for n in names if n.endswith(('.fwsc', '.elf', '.o', '.a')) or n.endswith('app.bin')]
assert not binaries, f'generated binaries in the source: {binaries[:5]}'

report = {'commit': full, 'tar': tar.name, 'tar_bytes': len(data), 'tar_sha256': digest, 'files': len(names),
          'prefix': prefix.rstrip('/'), 'licences': sorted(n for n in names if n.startswith('LICENSES/')) + ['LICENSE', 'LICENSING.md']}
if build:
    ext = out / 'extract'; ext.mkdir()
    with tarfile.open(tar) as t: t.extractall(ext)
    src = ext / prefix.rstrip('/')
    b = src / 'big-beat/integration/build'
    nat = out / 'native'
    r = subprocess.run(['bash', str(b / 'run_build.sh'), str(src), str(b / 'cmds-r4-onepass'),
                        str(b / 'recipes/composition-20-lean'), str(nat)], capture_output=True, text=True, env=os.environ)
    (out / 'native-build.log').write_text(r.stdout + r.stderr)
    assert r.returncode == 0, 'clean build of the extracted source failed (see native-build.log)'
    app = json.loads((nat / 'report.json').read_text())['candidate']
    report['rebuilt_app'] = {k: app[k] for k in ('sha256', 'bytes', 'code_free', 'ram_free', 'pool_free')}
    if expect: assert app['sha256'] == expect, f"extracted source builds {app['sha256']}, not the expected {expect}"
    report['reproduces_expected_app'] = bool(expect) and app['sha256'] == expect
(out / 'SOURCE-BUNDLE.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
