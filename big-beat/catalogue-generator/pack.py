#!/usr/bin/env python3
"""r9 catalogue pack: Joe-approved content into rc2's catalogue (r9 PLAN step 1). Host only, no device.

    python3 claude-code/r9/catalogue/pack.py            # from the repository root; one command, reproducible

Base     rc2 = source/rc2-canonical fc9e196, firmware/src/bb/bb_performance_catalogue.inc (173,387 B BBPC v2,
         boot42's catalogue + 138 Roland factory presets appended, commit 650afce). Read with `git show`.
Gate 1   Before anything else, bbpc.encode() must rebuild rc2's catalogue byte for byte from JSON: boot42's
         dump (ceb0986) encoded canonically, rc2's dump appended as append_catalogue.py does, every page packed
         with repack_catalogue.mjs's shortest-parse LZ4. Both the BBPC bytes and the .inc text must match.
Content  (all approved by Joe 11:24 / 18:52; the source files are read as they are in the working tree)
  Dillo     claude/dillo/pilot-77-motorik.json, claude/dillo/eras/*.json (21 eras, by part letter) and
            claude/dillo/songs/*.json (21 songs, by catalogue part order; names must match); lengths.dillo = len.
  Refits    claude/drums/<era>-drums.json "dillo_refit": steps_changed take the refit text's notes (absent = rest),
            then every rest carries the last sounding note (the drum libraries' apply_refit convention).
            Cross-checked against the record's full_line / line where it has one.
  Expression  song steps keep rc2's velocity/gate by step index (rc2 = boot42 = Codex's 11:57 derivation).
            --song-expression positional instead copies each part's lead velocity/gate by step position
            (the boot-28 authoring convention, where bass and lead shared one per-step profile).
  Presets   era dillo.settings <- settings_suggested where a value differs; 'saw' is written 'sawtooth', the only
            saw spelling bb_factory_recipe.c:56 accepts. Song suggestions are listed only (no per-song preset
            field is read by the firmware; song.settings is a mixer snapshot).
  Pads/lead claude/telegram/*.json overrides; else Egypt's pads_lead_proposed (+4). Telegram wins, as harmony_check.
  Drums     claude/drums/<era>-drums.json (pilot-77-motorik-drums.json for 77) -> pattern.events, in the format
            Codex used for the approved Motorik/bb-study drums (merge_approved_patterns.py), flams as the
            catalogue's own flam events (modifiers ["flam"], source_role "flam_principal"), accents as ACCENT
            events. A part whose grid already equals rc2's keeps rc2's events untouched. era.swing follows the
            drum file (89 Detroit 0 -> 15, approved).
Output   --mode compact (default): canonical encode of the r9 JSON alone, the same record rules that rebuild boot39's
         and boot42's catalogues byte for byte. rc2's 2 unreachable records go and nothing new is stranded.
         --mode append: r9 appended to rc2 as rc2 was appended to boot42. Every rc2 record keeps its ID, but each
         replaced array stays behind unreachable (+13 KB). Record IDs are never persisted (bb_session.h stores
         explicit fields; bb_factory_choice lives in RAM), so renumbering is safe across an update.
Hold     --hold ERA keeps rc2's drums and skips the Dillo refit for that era (e.g. 82-synth-pop, pending Joe's ear).
         Writes into --out (default claude-code/r9/catalogue/out): bb_performance_catalogue.inc, r9 JSON dump,
         rc2.patch (git apply on fc9e196), pack-report.json, inputs.json (PR #45 "inputs" entries).
"""
import argparse
import copy
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bbpc  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
RC2_COMMIT = 'fc9e196ddba065084152fb873cb50d7c30177af7'
BOOT42_COMMIT = 'ceb0986'
INC_PATH = 'firmware/src/bb/bb_performance_catalogue.inc'
RC2_INC_SHA = None          # .inc text hash, filled from git and reported
RC2_BBPC_SHA = '6ca377ee4908c7081f68e1f539ffad700d03e5c9a8a75dbbf21b91bc0fa8a51d'
BOOT42_BBPC_SHA = 'fe855eba73c3e9b9837a1808c2dcc50315c3bfea3553370f3a35e2c98cfe6ca8'
INC_COMMENT = 'Losslessly repacked BBPC v2; same 768-byte pages and complete record IDs.'
R9_COMMENT = ('r9 catalogue: rc2 + Joe-approved Dillo, pads/lead, era drums and refits; '
              'claude-code/r9/catalogue/pack.py (BBPC v2, 768-byte pages).')
NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def git_show(rev, path):
    return subprocess.run(['git', '-C', REPO, 'show', f'{rev}:{path}'], check=True, capture_output=True).stdout


def rel(p):
    return os.path.relpath(p, REPO).replace(os.sep, '/')


def load(path, inputs):
    data = open(path, 'rb').read()
    inputs.append({'name': rel(path), 'path': rel(path), 'sha256': sha(data), 'bytes': len(data)})
    return json.loads(data)


def midi(name):
    m = re.fullmatch(r'([A-G])([#b]?)(-?\d+)', name)
    if not m:
        raise ValueError('bad note name ' + name)
    n = NAMES[m.group(1)] + {'#': 1, 'b': -1, '': 0}[m.group(2)]
    return n + 12 * (int(m.group(3)) + 1)


def parse_line(txt):
    out = {}
    for tok in txt.split():
        s, n = tok.split(':')
        out[int(s)] = (midi(n.replace('!', '').replace('~', '')), '!' in n, '~' in n)
    return out


def line_text(bass):
    nm = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']
    return ' '.join(f"{i + 1}:{nm[s['note'] % 12]}{s['note'] // 12 - 1}{'!' if s['accent'] else ''}{'~' if s['slide'] else ''}"
                    for i, s in enumerate(bass) if s['on'])


def norm_line(line):
    """parse result of a line_text, for spelling-free comparison."""
    return {s: (n, a, sl) for s, (n, a, sl) in parse_line(line).items()}


def apply_refit(bass, rec, label):
    """rec: {approved, refit, steps_changed, ...}. Verifies the record against the approved line first."""
    old = {i + 1: (s['note'], s['accent'], s['slide']) for i, s in enumerate(bass) if s['on']}
    appr, new, changed = parse_line(rec['approved']), parse_line(rec['refit']), set(rec['steps_changed'])
    for s, v in appr.items():
        if old.get(s) != v:
            raise ValueError(f'{label}: refit "approved" step {s} {v} != approved line {old.get(s)}')
    for s, v in new.items():
        if s not in changed and old.get(s) != v:
            raise ValueError(f'{label}: refit step {s} outside steps_changed differs from the approved line')
    lo, hi = min(changed), max(changed)
    for s in range(lo, hi + 1):
        if s in old and s not in appr and s not in new:
            raise ValueError(f'{label}: approved line has step {s} inside the refit span but the record omits it')
    out = copy.deepcopy(bass)
    for s in sorted(changed):
        if s in new:
            n, a, sl = new[s]
            out[s - 1] = dict(out[s - 1], note=n, on=True, accent=a, slide=sl)
        else:
            out[s - 1] = dict(out[s - 1], on=False, accent=False, slide=False)
    last = out[0]['note']
    for st in out:                      # rest carries the last sounding note (drum libraries' apply_refit)
        if st['on']:
            last = st['note']
        else:
            st['note'] = last
    full = rec.get('full_line') or rec.get('line')
    if full and norm_line(full) != norm_line(line_text(out)):
        raise ValueError(f'{label}: refit result does not equal the record full_line')
    return out


def drum_grid_from_events(events):
    hits, acc = {}, set()
    for e in events:
        if e['voice'] in ('ACCENT', 'ACC'):
            acc.add(e['step'])
            continue
        hits[(e['voice'], e['step'])] = (e.get('intensity') == 'accented', 'flam' in (e.get('modifiers') or []))
    return hits, acc


def drum_grid_from_source(p):
    acc_hits = p.get('accent_hits', {})
    flams = p.get('flams') or {}
    hits = {}
    for v, ss in p['drums'].items():
        for s in ss:
            hits[(v, s)] = (s in acc_hits.get(v, []), s in flams.get(v, []))
    return hits, set(p.get('accents', []))


def drum_events(p):
    acc_hits = p.get('accent_hits', {})
    flams = p.get('flams') or {}
    for v, ss in flams.items():
        if not set(ss) <= set(p['drums'].get(v, [])):
            raise ValueError(f"flam without hit {v} {ss}")
    ev = []
    for v, ss in p['drums'].items():
        if len(ss) != len(set(ss)):
            raise ValueError(f'duplicate hits {v}')
        for s in ss:
            e = {'voice': v, 'step': s, 'role': 'playable_voice', 'source_voice': v,
                 'source_role': 'flam_principal' if s in flams.get(v, []) else 'approved Big Beat arrangement'}
            if v in acc_hits:
                e['intensity'] = 'accented' if s in acc_hits[v] else 'unaccented'
            if s in flams.get(v, []):
                e['modifiers'] = ['flam']
            ev.append(e)
    for s in p.get('accents', []):
        ev.append({'voice': 'ACCENT', 'step': s, 'role': 'accent_control', 'source_voice': 'ACCENT',
                   'source_role': 'accent_control'})
    return ev


def build_r9(rc2, inputs, song_expression='keep', hold=()):
    cat = copy.deepcopy(rc2)
    log = []
    eras = {e['id']: e for e in cat['eras']}
    songs = {s['song']: s for s in cat['songs']}
    D = os.path.join(REPO, 'claude', 'dillo')
    T = os.path.join(REPO, 'claude', 'telegram')
    DR = os.path.join(REPO, 'claude', 'drums')
    expected = {'dillo': {}, 'pads_lead': {}, 'drums': {}}

    # drums first (they carry the refits)
    drum_files = sorted(glob.glob(os.path.join(DR, '*-drums.json')))
    refits = {}
    seen_drums = set()
    for f in drum_files:
        S = load(f, inputs)
        e = eras[S['era']]
        if S['era'] in seen_drums:
            raise ValueError('two drum files for ' + S['era'])
        seen_drums.add(S['era'])
        if S['era'] in hold:            # drums and their Dillo refit stay as in rc2
            log.append(('held', S['era']))
            refits[S['era']] = {}
            continue
        if e['bpm'] != S['bpm']:
            raise ValueError(f"{S['era']}: bpm {e['bpm']} -> {S['bpm']} is not in the approved scope")
        if e['swing'] != S['swing']:
            log.append(('era_swing', S['era'], e['swing'], S['swing']))
            e['swing'] = S['swing']
        if [(p['letter'], p['name']) for p in e['parts']] != [(p['letter'], p['name']) for p in S['parts']]:
            raise ValueError(S['era'] + ': drum part letters/names differ')
        for c, p in zip(e['parts'], S['parts']):
            n = c['lengths']['drums']
            steps = [s for ss in p['drums'].values() for s in ss] + list(p.get('accents', []))
            if not steps or min(steps) < 1 or max(steps) > n:
                raise ValueError(f"{S['era']} {p['letter']}: steps outside 1..{n}")
            if drum_grid_from_events(c['pattern']['events']) != drum_grid_from_source(p):
                c['pattern']['events'] = drum_events(p)
                log.append(('drums', S['era'], p['letter']))
                if drum_grid_from_events(c['pattern']['events']) != drum_grid_from_source(p):
                    raise AssertionError('drum event round trip')
            hits, acc = drum_grid_from_source(p)
            expected['drums'][f"{S['era']}/{p['letter']}"] = {
                'hits': sorted([v, s, a, fl] for (v, s), (a, fl) in hits.items()), 'accents': sorted(acc)}
        refits[S['era']] = S.get('dillo_refit') or {}
    if seen_drums != set(eras):
        raise ValueError(f'drum files missing for {sorted(set(eras) - seen_drums)}')

    # Dillo eras
    era_files = [os.path.join(D, 'pilot-77-motorik.json')] + sorted(glob.glob(os.path.join(D, 'eras', '*.json')))
    seen = set()
    suggestions = []
    for f in era_files:
        P = load(f, inputs)
        e = eras[P['era']]
        seen.add(P['era'])
        cps = {p['letter']: p for p in e['parts']}
        if sorted(cps) != sorted(p['letter'] for p in P['parts']):
            raise ValueError(P['era'] + ': part letters differ')
        for p in P['parts']:
            c = cps[p['letter']]
            if c['name'] != p['name']:
                raise ValueError(f"{P['era']} {p['letter']}: name {c['name']} != {p['name']}")
            line = copy.deepcopy(p['bass'])
            if p['letter'] in refits.get(P['era'], {}):
                line = apply_refit(line, refits[P['era']][p['letter']], f"{P['era']} {p['letter']}")
                log.append(('refit', P['era'], p['letter']))
            if len(line) not in (16, 32, 64):
                raise ValueError('unsupported Dillo length')
            if any(set(s) != {'note', 'on', 'accent', 'slide'} for s in c['bass']) or \
               any(set(s) != {'note', 'on', 'accent', 'slide'} for s in line):
                raise ValueError(f"{P['era']} {p['letter']}: era steps carry fields other than note/on/accent/slide")
            if c['bass'] != line or c['lengths']['dillo'] != len(line):
                log.append(('dillo', P['era'], p['letter'], len(c['bass']), len(line)))
            c['bass'] = line
            c['lengths']['dillo'] = len(line)
            expected['dillo'][f"era/{P['era']}/{p['letter']}"] = line
        sug = P.get('settings_suggested') or {}
        cur = e['dillo']['settings']
        for k, v in sug.items():
            if k not in cur:
                suggestions.append(dict(scope='era', id=P['era'], key=k, current=None, suggested=v, applied=False,
                                        why='no such field in the era preset'))
                continue
            w = 'sawtooth' if (k == 'wave' and v == 'saw') else v
            if cur[k] != w:
                suggestions.append(dict(scope='era', id=P['era'], key=k, current=cur[k], suggested=v, written=w, applied=True))
                cur[k] = w
    if seen != set(eras):
        raise ValueError(f'Dillo eras missing {sorted(set(eras) - seen)}')

    # Dillo songs
    seen = set()
    egypt = {}
    for f in sorted(glob.glob(os.path.join(D, 'songs', '*.json'))):
        P = load(f, inputs)
        s = songs[P['song']]
        seen.add(P['song'])
        if [p['name'] for p in P['parts']] != [p['name'] for p in s['parts']]:
            raise ValueError(P['song'] + ': part names/order differ from the catalogue')
        for k, (p, c) in enumerate(zip(P['parts'], s['parts'])):
            line = copy.deepcopy(p['bass'])
            if len(line) not in (16, 32, 64):
                raise ValueError('unsupported Dillo length')
            old = c['bass']
            for i, st in enumerate(line):
                if song_expression == 'keep':
                    src = old[i] if len(old) == len(line) else old[i % len(old)]
                else:
                    src = c['lead'][i % c['lengths']['telegram']]
                st['velocity'], st['gate'] = src['velocity'], src['gate']
            if old != line or c['lengths']['dillo'] != len(line):
                log.append(('dillo', P['song'], c['name'], len(old), len(line)))
            c['bass'] = line
            c['lengths']['dillo'] = len(line)
            expected['dillo'][f"song/{P['song']}/{k}"] = line
        if P.get('pads_lead_proposed'):
            egypt[P['song']] = P['pads_lead_proposed']
        for k2, v in (P.get('settings_suggested') or {}).items():
            cur = s['settings'].get(k2)
            if cur != v and not (k2 == 'wave' and v == 'saw' and cur == 'sawtooth'):
                suggestions.append(dict(scope='song', id=P['song'], key=k2, current=cur, suggested=v, applied=False,
                                        why='firmware reads no per-song Dillo preset (song.settings is not read)'))
    roland = [s['song'] for s in cat['songs'] if s['song'] not in seen]
    if len(seen) != 21:
        raise ValueError(f'expected 21 Dillo songs, got {len(seen)}')

    # pads and lead: telegram overrides take precedence over a song file's own proposal (harmony_check.py)
    over = {}
    for f in sorted(glob.glob(os.path.join(T, '*.json'))):
        X = load(f, inputs)
        if 'pads_lead_proposed' not in X:
            continue
        key = ('era', X['era']) if X.get('era') else ('song', X['song'])
        if key in over:
            raise ValueError(f'two overrides for {key}')
        over[key] = X['pads_lead_proposed']
    for name, prop in egypt.items():
        over.setdefault(('song', name), prop)
    for (kind, ident), prop in sorted(over.items()):
        group = eras[ident] if kind == 'era' else songs[ident]
        if len(prop) != len(group['parts']):
            raise ValueError(f'{ident}: proposal covers {len(prop)} of {len(group["parts"])} parts')
        for k, (c, p) in enumerate(zip(group['parts'], prop)):
            if c['name'] != p['name'] or (kind == 'era' and c['letter'] != p['letter']):
                raise ValueError(f'{ident}: proposal part {k} is {p["name"]}, catalogue {c["name"]}')
            for key, lk in (('greyhound', 'greyhound'), ('lead', 'telegram')):
                if len(p[key]) != c['lengths'][lk] or ('lengths' in p and p['lengths'][lk] != c['lengths'][lk]):
                    raise ValueError(f'{ident} {c["name"]}: {key} length changes')
            if kind == 'era':
                g = {'name': p.get('greyhound_name', c['greyhound']['name']), 'notes': copy.deepcopy(p['greyhound'])}
                if g['name'] != c['greyhound']['name']:
                    raise ValueError(f'{ident} {c["name"]}: greyhound name changes')
            else:
                g = copy.deepcopy(p['greyhound'])
            if c['greyhound'] != g or c['lead'] != p['lead']:
                log.append(('pads_lead', ident, c['name']))
            c['greyhound'] = g
            c['lead'] = copy.deepcopy(p['lead'])
            expected['pads_lead'][f'{kind}/{ident}/{k}'] = {'greyhound': g, 'lead': p['lead']}
    return cat, log, suggestions, expected, roland


def write_patch(old_text, new_text, out_path):
    with tempfile.TemporaryDirectory() as t:
        for side, text in (('a', old_text), ('b', new_text)):
            p = os.path.join(t, side, INC_PATH)
            os.makedirs(os.path.dirname(p))
            open(p, 'w').write(text)
        r = subprocess.run(['git', 'diff', '--no-index', '--full-index', '--no-color', 'a', 'b'], cwd=t,
                           capture_output=True, text=True)
        if r.returncode not in (0, 1):
            raise RuntimeError(r.stderr)
        txt = r.stdout.replace('a/a/', 'a/').replace('b/b/', 'b/')
        txt = re.sub(r'^diff --git a/a/(\S+) b/b/(\S+)', r'diff --git a/\1 b/\2', txt, flags=re.M)
        open(out_path, 'w').write(txt)
    return txt


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=os.path.join(HERE, 'out'))
    ap.add_argument('--mode', choices=('append', 'compact'), default='compact')
    ap.add_argument('--song-expression', choices=('keep', 'positional'), default='keep')
    ap.add_argument('--hold', action='append', default=[], metavar='ERA',
                    help="keep rc2's drums (and no Dillo refit) for this era, e.g. --hold 82-synth-pop")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    inputs = []

    rc2_text = git_show(RC2_COMMIT, INC_PATH).decode()
    b42_text = git_show(BOOT42_COMMIT, INC_PATH).decode()
    rc2_bytes, b42_bytes = bbpc.read_inc_text(rc2_text), bbpc.read_inc_text(b42_text)
    if sha(rc2_bytes) != RC2_BBPC_SHA or sha(b42_bytes) != BOOT42_BBPC_SHA:
        raise SystemExit('base catalogue hash mismatch')
    inputs.insert(0, {'name': 'rc2 bb_performance_catalogue.inc', 'path': f'git:{RC2_COMMIT}:{INC_PATH}',
                      'sha256': sha(rc2_text.encode()), 'bytes': len(rc2_text.encode())})
    inputs.insert(1, {'name': 'boot42 bb_performance_catalogue.inc (gate 1 only)', 'path': f'git:{BOOT42_COMMIT}:{INC_PATH}',
                      'sha256': sha(b42_text.encode()), 'bytes': len(b42_text.encode())})
    rc2 = bbpc.decode(rc2_bytes)
    b42 = bbpc.decode(b42_bytes)

    # Gate 1: the encoder rebuilds rc2 byte for byte from JSON
    again, rep_rc2 = bbpc.encode(b42, appends=[rc2])
    gate1 = again == rc2_bytes and bbpc.inc_text(again, INC_COMMENT) == rc2_text
    print('gate 1 encoder reproduces rc2:', 'PASS' if gate1 else 'FAIL', sha(again))
    if not gate1:
        raise SystemExit('gate 1 failed: encoder does not reproduce rc2; nothing written')

    r9, log, suggestions, expected, roland = build_r9(rc2, inputs, a.song_expression, set(a.hold))
    if a.mode == 'append':
        blob, rep = bbpc.encode(b42, appends=[rc2, r9])
        if blob[:0] != b'':
            raise AssertionError
    else:
        blob, rep = bbpc.encode(r9)
    if bbpc.decode(blob) != r9:
        raise SystemExit('decode(encode(r9)) != r9')
    compact_bytes = len(bbpc.encode(r9)[0]) if a.mode == 'append' else len(blob)
    inc = bbpc.inc_text(blob, R9_COMMENT)
    open(os.path.join(a.out, 'bb_performance_catalogue.inc'), 'w').write(inc)
    json.dump(r9, open(os.path.join(a.out, 'cat-r9.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    json.dump(expected, open(os.path.join(a.out, 'expected.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    patch = write_patch(rc2_text, inc, os.path.join(a.out, 'rc2-catalogue.patch'))
    kinds = {}
    for row in log:
        kinds[row[0]] = kinds.get(row[0], 0) + 1
    report = {
        'base': {'commit': RC2_COMMIT, 'bbpc_bytes': len(rc2_bytes), 'bbpc_sha256': sha(rc2_bytes),
                 'inc_sha256': sha(rc2_text.encode())},
        'gate1_encoder_reproduces_rc2': gate1, 'gate1_report': rep_rc2,
        'mode': a.mode, 'song_expression': a.song_expression, 'held_eras': sorted(a.hold),
        'r9': dict(rep, bbpc_sha256=sha(blob), inc_sha256=sha(inc.encode()), inc_bytes=len(inc.encode()),
                   patch_sha256=sha(patch.encode()), compact_alternative_bytes=compact_bytes),
        'delta_bytes': len(blob) - len(rc2_bytes),
        'changes_by_kind': kinds, 'changes': [list(r) for r in log],
        'preset_suggestions': suggestions, 'untouched_roland_songs': roland,
    }
    json.dump(report, open(os.path.join(a.out, 'pack-report.json'), 'w'), indent=1, ensure_ascii=False)
    json.dump(inputs, open(os.path.join(a.out, 'inputs.json'), 'w'), indent=1)
    print(json.dumps({k: report[k] for k in ('mode', 'delta_bytes', 'changes_by_kind')}),
          f"r9 {len(blob)} B sha {sha(blob)[:16]}  compact alternative {compact_bytes} B")


if __name__ == '__main__':
    main()
