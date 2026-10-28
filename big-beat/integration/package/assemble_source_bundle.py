#!/usr/bin/env python3
"""Assemble the GPL-3.0-only corresponding-source bundle for a Big Beat candidate (offline, read-only inputs).

Layout of the bundle (one directory, then a deterministic .tar: sorted names, mtime 0, uid/gid 0):
  firmware/ build/ LICENSE LICENSES/ LICENSING.md   the exact candidate tree (materialised by the r9 recipe)
  tools/ web/ assets/ docs/ build.sh BUILDING.md    Felucca build tools and web files from the Big Beat target tree
  BIG_BEAT_PATCH_MANIFEST.json                      (upstream Felucca ef1f561 plus the Big Beat build.py / gen_samples.py deltas)
  big-beat/                                          this repository's recipe: from-source build, verifier, packaging,
                                                     the r9 patch series and HANDOFF.json, the catalogue generator
                                                     with its pinned input list (the music sources stay in Git, by hash),
                                                     the song generator and its extracted grids (songs/), every
                                                     per-release folder (r9.*) and the timing fixture the sweep reads
  SOURCE-BUNDLE-MANIFEST.json                        every file's SHA-256 and size, and the input identities

usage: assemble_source_bundle.py CANDIDATE_TREE OUT_DIR [--public CATALOGUE.json]
  (OUT_DIR must not exist; run from the review repo root)
  --public CATALOGUE.json  the bundle for the public firmware repository: the catalogue's editable source is the
           full catalogue JSON (encode it with catalogue-generator/bbpc.py), in place of the song generator, its grids
           and the per-release notes; the internal notes are left out, and the bundle must not name the
           transcription source anywhere (Joe, 28 Oct). The verifier and its fixtures are identical."""
from pathlib import Path
import hashlib, io, json, os, re, shutil, subprocess, sys, tarfile

OB = Path('/Volumes/Orange Base/Codex Workspaces')
TARGET = OB / 'big-beat-fm1/build/target/Felucca-BigBeat-FM1-preset-face-r1'
UPSTREAM = OB / 'fm1-build-setup/Felucca-ef1f561b6b105f20350f6b1eac0dc0789a357f00'
TOOL_DELTAS = {'tools/build.py': '7b083ab04fb16a24468e92d40896ec5c9ed47bf11e5226b0db7a534ac960ce46',
               'tools/gen_samples.py': '7064c8b63420decfee7a2d7335738bdcf1c42958011fafeec18efe5162db3b40'}
CATALOGUE_COMMIT = '75dd228'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
NAME = bytes.fromhex('70696b616e74').decode()   # the transcription source's name, spelled so this file can travel in the bundle
skip = lambda p: '__pycache__' in p.parts or p.name == '.DS_Store'

args = sys.argv[1:]
public = None
if '--public' in args:
    i = args.index('--public'); public = Path(args[i + 1]).resolve(); del args[i:i + 2]
cand, out = Path(args[0]).resolve(), Path(args[1]).resolve()
root = Path(subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True, check=True).stdout.strip())
assert not out.exists(); B = out / 'bundle'; B.mkdir(parents=True)

def copy_tree(src, dst):
    for p in sorted(Path(src).rglob('*')):
        if p.is_file() and not skip(p.relative_to(src)):
            q = dst / p.relative_to(src); q.parent.mkdir(parents=True, exist_ok=True); shutil.copy(p, q)

# 1. the exact candidate tree (tracked files only)
files = subprocess.run(['git', 'ls-files'], cwd=cand, capture_output=True, text=True, check=True).stdout.split('\n')
for f in filter(None, files):
    q = B / f; q.parent.mkdir(parents=True, exist_ok=True); shutil.copy(cand / f, q)
inv = '\n'.join(f'{f} {sha(cand / f)}' for f in sorted(f for f in files if f.startswith(('build/', 'firmware/'))))
tree = hashlib.sha256(inv.encode()).hexdigest()

# 2. Felucca tools and web from the Big Beat target tree; check it is upstream ef1f561 plus the two declared deltas
for d in ('tools', 'web', 'assets', 'docs'):
    copy_tree(TARGET / d, B / d)
for f in ('build.sh', 'BUILDING.md', 'BIG_BEAT_PATCH_MANIFEST.json'):
    shutil.copy(TARGET / f, B / f)
for p in sorted((UPSTREAM / 'tools').rglob('*')) + sorted((UPSTREAM / 'web').rglob('*')):
    if not p.is_file() or skip(p.relative_to(UPSTREAM)): continue
    rel = str(p.relative_to(UPSTREAM)); mine = sha(B / rel)
    assert mine == (TOOL_DELTAS.get(rel) or sha(p)), f'{rel}: target tree differs from upstream beyond the declared deltas'
assert sha(B / 'build.sh') == sha(UPSTREAM / 'build.sh')

# 3. this repository's recipe
I = root / 'claude-code/integration'
for d in ('build', 'package', 'tests', 'r9-step1'):
    copy_tree(I / d, B / 'big-beat/integration' / d)
# r9.10 (Duke, #4 6474414163): every file the verifier and its harnesses read must travel with it.
#  - route-matrix.csv: host-tests compares the route matrix against it
#  - timing/tests/integration/command.json: tests/sweep/build_sweep.py re-roots it (../../../timing from tests/sweep)
#  - songs/: the song generator, its extracted grids and manifest (r9.4+ catalogue; the source PDFs stay with
#    their owner and are pinned by SHA-256 in songs-report*.json). Public bundle: the catalogue JSON instead.
#  - the per-release notes (r9.* folders: patches, verify summaries, READMEs) and the audit/timing write-ups
notes = () if public else ('HANDOFF-R9.md', 'KNOWN-ISSUES-R9.md', 'README.md', 'AUDIT-R9.7.md', 'TIMING-OPTIONS-R9.9.md')
for f in ('verify_release.py', 'inputs.json', 'route-matrix.csv') + notes:
    if (I / f).exists(): shutil.copy(I / f, B / 'big-beat/integration' / f)
if public:
    (B / 'big-beat/catalogue').mkdir(parents=True)
    shutil.copy(public, B / 'big-beat/catalogue/catalogue.json')
else:
    for d in ['songs'] + sorted(p.name for p in I.glob('r9.*') if p.is_dir()):
        copy_tree(I / d, B / 'big-beat/integration' / d)
fixture = root / 'claude-code/timing/tests/integration/command.json'
(B / 'big-beat/timing/tests/integration').mkdir(parents=True, exist_ok=True)
shutil.copy(fixture, B / 'big-beat/timing/tests/integration/command.json')
need = ['big-beat/integration/route-matrix.csv', 'big-beat/timing/tests/integration/command.json']
need += ['big-beat/catalogue/catalogue.json'] if public else ['big-beat/integration/songs/tools/build_songs.py', 'big-beat/integration/songs/manifest.json']
for n in need:
    assert (B / n).is_file(), f'source bundle is missing {n}'
if not public:
    assert len(list((B / 'big-beat/integration/songs/extracted').glob('*.json'))) >= 46, 'song grids missing'
shutil.copy(root / 'HANDOFF.json', B / 'big-beat/HANDOFF.json')
lic = (I / 'BIG-BEAT-LICENSING.md').read_text()
if public:   # the transcription permission is held privately by the owner; the row stays out of the public file
    lic = '\n'.join(l for l in lic.split('\n') if not re.search(NAME, l, re.I)) + ('' if lic.endswith('\n') else '')
(B / 'BIG-BEAT-LICENSING.md').write_text(lic)
if not public: shutil.copy(I / 'BIG-BEAT-LICENSING.md', B / 'big-beat/integration/BIG-BEAT-LICENSING.md')
cat = B / 'big-beat/catalogue-generator'; cat.mkdir(parents=True)
for f in ('pack.py', 'bbpc.py', 'gates.py', 'probe.c', 'audio_account.py', 'README.md', 'out/inputs.json'):
    data = subprocess.run(['git', 'show', f'{CATALOGUE_COMMIT}:claude-code/r9/catalogue/{f}'], cwd=root, capture_output=True, check=True).stdout
    (cat / f).parent.mkdir(parents=True, exist_ok=True); (cat / f).write_bytes(data)

if public:   # nothing in the public bundle may name the transcription source, in text or in compiled tables
    bad = [str(p.relative_to(B)) for p in sorted(B.rglob('*')) if p.is_file() and re.search(NAME.encode(), p.read_bytes(), re.I)]
    for form in (NAME, NAME.capitalize(), NAME.upper()):
        hexname = ','.join('0x%02x' % c for c in form.encode())
        bad += [str(p.relative_to(B)) for p in sorted(B.rglob('*.inc')) if hexname in p.read_text(errors='ignore').lower().replace(' ', '')]
    assert not bad, f'public bundle still names the transcription source: {bad[:10]}'

# 4. manifest and deterministic tar
entries = {str(p.relative_to(B)): {'sha256': sha(p), 'bytes': p.stat().st_size} for p in sorted(B.rglob('*')) if p.is_file()}
manifest = {'what': 'GPL-3.0-only corresponding source for the Big Beat FM-1 candidate',
            'candidate_tree_sha256': tree, 'handoff_sha256': sha(root / 'HANDOFF.json'),
            'felucca_upstream': {'snapshot': str(UPSTREAM), 'identity_note': 'ef1f561 is the custody name of a snapshot without .git, not verified Git history'},
            'big_beat_tool_deltas': TOOL_DELTAS, 'catalogue_generator_commit': CATALOGUE_COMMIT,
            'public': bool(public),
            'not_included': ['Jieli toolchain and AC79 SDK (pinned by hash in big-beat/integration/inputs.json; redistribution terms unconfirmed)',
                             'music source data under claude/ at the generator commit (pinned in catalogue-generator/out/inputs.json)',
                             'the sampled-kit sound library (not GPL source; redistribution authorised by the owner, see BIG-BEAT-LICENSING.md)'],
            'files': entries}
(B / 'SOURCE-BUNDLE-MANIFEST.json').write_text(json.dumps(manifest, indent=1) + '\n')
tar = out / 'big-beat-source.tar'
with tarfile.open(tar, 'w', format=tarfile.PAX_FORMAT) as t:
    for p in sorted(B.rglob('*')):
        if not p.is_file(): continue
        info = tarfile.TarInfo('big-beat-source/' + str(p.relative_to(B))); data = p.read_bytes()
        info.size, info.mtime, info.mode, info.uid, info.gid = len(data), 0, 0o755 if os.access(p, os.X_OK) else 0o644, 0, 0
        t.addfile(info, io.BytesIO(data))
print(json.dumps({'files': len(entries) + 1, 'candidate_tree_sha256': tree, 'tar': str(tar), 'tar_sha256': sha(tar),
                  'tar_bytes': tar.stat().st_size, 'manifest_sha256': sha(B / 'SOURCE-BUNDLE-MANIFEST.json')}, indent=1))
