#!/usr/bin/env python3
"""r9 catalogue gates 2-5 (host only). Run after pack.py, from the repository root:

    python3 claude-code/r9/catalogue/gates.py [--out claude-code/r9/catalogue/out] [--work DIR]

G2  The firmware's own decoder (claude/tests/dillo-audit/dump.c compiled against rc2's bb_catalogue*) re-dumps
    the new .inc; the dump equals pack.py's r9 JSON, and every packed Dillo line, pad/lead array and drum grid
    equals its source (expected.json).
G3  check_dillo.py (0 failures); harmony_check.py twice: with the source lines and telegram overrides (as its
    authors run it) and with the catalogue's own lines, pads and lead only (Dillo tree rebuilt from the dump,
    no overrides); claude/telegram/check_pads_lead.py pointed at the dump; check_drums.py on every era.
G4  Field diff rc2 -> r9 over the whole dump: every changed leaf must be in the allowed set (Dillo bass and
    lengths.dillo, pads/lead, era pattern.events, era dillo.settings, era swing); the Roland songs, every song
    and era field outside that set, and the top level are identical.
G5  Firmware read paths: probe.c (rc2's loaders) over every section of rc2 and r9: no read, rich-phrase, synth
    or recipe result gets worse; no new trio-projection or 909-projection issue is unaccounted; record limits.
Writes gates.json and diff-report.json into --out.
"""
import argparse, collections, copy, csv, glob, hashlib, io, json, os, re, shutil, subprocess, sys, contextlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
import bbpc  # noqa: E402
RC2 = 'fc9e196ddba065084152fb873cb50d7c30177af7'
CODE_FREE_RC2 = 18348          # claude-code/integration/evidence/rc2/build-report.json (codex/rc2-release-docs)
APP_SLOT = 0x8DFBC             # firmware/app.ld XIP LENGTH
PROBE_SRCS = ('bb/bb_catalogue.c bb/bb_paged_records.c bb/bb_paged_container.c bb/bb_catalogue_page.c bb/bb_crc32.c '
              'bb/bb_browser.c bb/bb_factory_drum.c bb/bb_factory_drum_launch.c bb/bb_phrase.c bb/bb_phrase_shaker.c '
              'bb/bb_phrase_index.c bb/bb_phrase_controls.c bb/bb_delayed.c bb/bb_factory_pattern.c bb/bb_note_expression.c '
              'bb/bb_note_expression_catalogue.c bb/bb_factory_recipe.c bb/bb909_sequence.c bb/bb_core.c bb/bb_wire.c bb/bb_factory_trio.c bb/bb_trio.c bb/bb_session.c').split()


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    return r.returncode, r.stdout, r.stderr


def defines():
    c = json.load(open(os.path.join(REPO, 'claude-code/timing/tests/integration/command.json')))
    return list(dict.fromkeys(x for x in c if x.startswith('-D')))


def build_tools(work):
    tree = os.path.join(work, 'rc2')
    if not os.path.isdir(tree):
        os.makedirs(tree)
        p = subprocess.Popen(['git', '-C', REPO, 'archive', RC2, 'firmware', 'build'], stdout=subprocess.PIPE)
        subprocess.run(['tar', '-x', '-C', tree], stdin=p.stdout, check=True)
        p.wait()
    src, bb = os.path.join(tree, 'firmware/src'), os.path.join(tree, 'firmware/src/bb')
    inc = ['-I' + bb, '-I' + bb + '/x0x', '-I' + src, '-I' + os.path.join(tree, 'build/gen'), '-I' + os.path.join(tree, 'firmware/hal')]
    probe = os.path.join(work, 'probe')
    rc, so, se = sh(['cc', '-std=c11', '-O1', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                     '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *defines(), *inc, '-o', probe,
                     os.path.join(HERE, 'probe.c'), *[os.path.join(src, s) for s in PROBE_SRCS]])
    if rc:
        raise SystemExit('probe build failed\n' + se[-3000:])
    return tree, probe


def c_dump(tree, inc_path, work, tag):
    d = os.path.join(work, 'dump-' + tag)
    os.makedirs(d, exist_ok=True)
    shutil.copy(inc_path, os.path.join(d, 'bb_performance_catalogue.inc'))
    bb = os.path.join(tree, 'firmware/src/bb')
    exe = os.path.join(d, 'dump')
    rc, so, se = sh(['cc', '-O2', '-std=c11', '-I' + d, '-I' + bb, '-I' + os.path.dirname(bb),
                     os.path.join(REPO, 'claude/tests/dillo-audit/dump.c'),
                     *[os.path.join(bb, f) for f in ('bb_catalogue.c', 'bb_paged_records.c', 'bb_paged_container.c',
                                                      'bb_catalogue_page.c', 'bb_crc32.c')], '-o', exe])
    if rc:
        raise SystemExit('dump build failed ' + se)
    rc, so, se = sh([exe])
    if rc:
        raise SystemExit('dump failed ' + se)
    path = os.path.join(d, 'cat.json')
    open(path, 'w').write(so)
    return path, json.loads(so)


def run_py(script, args, cwd=REPO, replace=None):
    """Run a check script; with replace=(old, new) the script's text is patched in memory (path redirection)."""
    if not replace:
        rc, so, se = sh([sys.executable, script, *args], cwd=cwd)
        return rc, so + se
    src = open(script).read()
    if replace[0] not in src:
        raise SystemExit(f'{script}: redirect target not found')
    src = src.replace(*replace)
    buf = io.StringIO()
    g = {'__file__': script, '__name__': '__main__'}
    old_argv, old_path = sys.argv, list(sys.path)
    sys.argv = [script, *args]
    sys.path.insert(0, os.path.dirname(script))
    rc = 0
    try:
        with contextlib.redirect_stdout(buf):
            exec(compile(src, script, 'exec'), g)
    except SystemExit as e:
        rc = e.code or 0
    finally:
        sys.argv, sys.path = old_argv, old_path
    return rc, buf.getvalue()


def tail(txt, n=3):
    return [l for l in txt.strip().splitlines()][-n:]


def leaves(x, path=''):
    if isinstance(x, dict):
        out = {path + '{}': '#keys:' + json.dumps(list(x.keys()))}
        for k, v in x.items():
            out.update(leaves(v, f'{path}.{k}'))
        return out
    if isinstance(x, list):
        out = {path + '[]': len(x)}
        for i, v in enumerate(x):
            out.update(leaves(v, f'{path}[{i}]'))
        return out
    return {path: json.dumps(x)}


ALLOWED = [
    r'^\.eras\[\d+\]\.parts\[\d+\]\.bass(\[\]|\[\d+\]\.\w+|\[\d+\]\{\})$',
    r'^\.eras\[\d+\]\.parts\[\d+\]\.lengths\.dillo$',
    r'^\.eras\[\d+\]\.parts\[\d+\]\.lead(\[\]|\[\d+\]\.\w+|\[\d+\]\{\})$',
    r'^\.eras\[\d+\]\.parts\[\d+\]\.greyhound\.notes(\[\]|\[\d+\].*)$',
    r'^\.eras\[\d+\]\.parts\[\d+\]\.pattern\.events(\[\]|\[\d+\].*)$',
    r'^\.eras\[\d+\]\.dillo\.settings\.\w+$',
    r'^\.eras\[\d+\]\.swing$',
    r'^\.songs\[(\d|1\d|20)\]\.parts\[\d+\]\.(bass|lead)(\[\]|\[\d+\]\.\w+|\[\d+\]\{\})$',
    r'^\.songs\[(\d|1\d|20)\]\.parts\[\d+\]\.greyhound(\[\]|\[\d+\].*)$',
    r'^\.songs\[(\d|1\d|20)\]\.parts\[\d+\]\.lengths\.dillo$',
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, 'out'))
    ap.add_argument('--work', default=os.path.join(HERE, 'out', 'work'))
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    G = {}
    rep = json.load(open(os.path.join(a.out, 'pack-report.json')))
    r9_json = json.load(open(os.path.join(a.out, 'cat-r9.json')))
    expected = json.load(open(os.path.join(a.out, 'expected.json')))
    tree, probe = build_tools(a.work)
    rc2_inc = os.path.join(tree, 'firmware/src/bb/bb_performance_catalogue.inc')
    r9_inc = os.path.join(a.out, 'bb_performance_catalogue.inc')
    rc2_path, rc2 = c_dump(tree, rc2_inc, a.work, 'rc2')
    dump_path, r9 = c_dump(tree, r9_inc, a.work, 'r9')

    # G1 recorded by pack.py
    G['G1 encoder reproduces rc2 byte for byte'] = dict(ok=rep['gate1_encoder_reproduces_rc2'],
                                                        detail=f"rc2 {rep['base']['bbpc_bytes']} B sha {rep['base']['bbpc_sha256']}")
    # G2
    bad = []
    if r9 != r9_json:
        bad.append('C dump != pack JSON')
    eras = {e['id']: e for e in r9['eras']}
    songs = {s['song']: s for s in r9['songs']}
    for k, line in expected['dillo'].items():
        kind, ident, part = k.split('/')
        if kind == 'era':
            got = next(p for p in eras[ident]['parts'] if p['letter'] == part)
        else:
            got = songs[ident]['parts'][int(part)]
        if got['bass'] != line or got['lengths']['dillo'] != len(line):
            bad.append('dillo ' + k)
    for k, v in expected['pads_lead'].items():
        kind, ident, part = k.split('/')
        got = (eras[ident] if kind == 'era' else songs[ident])['parts'][int(part)]
        if got['greyhound'] != v['greyhound'] or got['lead'] != v['lead']:
            bad.append('pads_lead ' + k)
    for k, v in expected['drums'].items():
        ident, letter = k.split('/')
        p = next(p for p in eras[ident]['parts'] if p['letter'] == letter)
        hits, acc = {}, set()
        for e in p['pattern']['events']:
            if e['voice'] in ('ACCENT', 'ACC'):
                acc.add(e['step'])
            else:
                hits[(e['voice'], e['step'])] = (e.get('intensity') == 'accented', 'flam' in (e.get('modifiers') or []))
        if sorted([v_, s, x, f] for (v_, s), (x, f) in hits.items()) != v['hits'] or sorted(acc) != v['accents']:
            bad.append('drums ' + k)
    G['G2 re-dump equals source JSON'] = dict(ok=not bad, detail=(
        f"C dump == pack JSON; {len(expected['dillo'])} Dillo lines, {len(expected['pads_lead'])} pad/lead parts, "
        f"{len(expected['drums'])} drum grids equal their sources") if not bad else bad[:20])

    # G3
    # The 138 Roland presets are drums only with one shared silent Dillo line (accompaniment: "Drums only; no
    # companion parts"), so check_dillo's R2/R4 fire on them in rc2 and r9 alike. Gate the 342 approved sections.
    approved = dict(r9, songs=[s for s in r9['songs'] if not s['song'].startswith('Roland ')])
    ap_path = os.path.join(a.work, 'cat-r9-approved-sections.json')
    json.dump(approved, open(ap_path, 'w'))
    rc, out = run_py(os.path.join(REPO, 'claude/tests/dillo-audit/check_dillo.py'), [ap_path])
    rc_all, out_all = run_py(os.path.join(REPO, 'claude/tests/dillo-audit/check_dillo.py'), [dump_path])
    rc_rc2, out_rc2 = run_py(os.path.join(REPO, 'claude/tests/dillo-audit/check_dillo.py'), [rc2_path])
    G['G3a check_dillo.py (342 approved sections)'] = dict(ok=rc == 0, detail=tail(out, 1) + [
        f"whole catalogue: {tail(out_all, 1)[0]}, rc2: {tail(out_rc2, 1)[0]} (all on the silent Roland drum-only lines)"])
    rc, out = run_py(os.path.join(REPO, 'claude/tests/dillo-audit/harmony_check.py'),
                     [dump_path, 'claude/dillo', 'claude/telegram'])
    G['G3b harmony_check.py (source lines + overrides)'] = dict(ok=rc == 0, detail=tail(out, 2))
    # catalogue-only harmony: Dillo tree rebuilt from the dump, no overrides, no Egypt proposal
    hd = os.path.join(a.work, 'harmony-from-dump')
    shutil.rmtree(hd, ignore_errors=True)
    os.makedirs(os.path.join(hd, 'dillo', 'eras'))
    os.makedirs(os.path.join(hd, 'dillo', 'songs'))
    os.makedirs(os.path.join(hd, 'telegram'))
    for e in r9['eras']:
        P = {'era': e['id'], 'parts': [{'letter': p['letter'], 'name': p['name'], 'bass': p['bass']} for p in e['parts']]}
        name = 'pilot-77-motorik.json' if e['id'] == '77-motorik' else os.path.join('eras', e['id'] + '.json')
        json.dump(P, open(os.path.join(hd, 'dillo', name), 'w'))
    for i, s in enumerate(r9['songs'][:21]):
        P = {'song': s['song'], 'parts': [{'letter': f'{k + 1:02d}', 'name': p['name'], 'bass': p['bass']}
                                         for k, p in enumerate(s['parts'])]}
        json.dump(P, open(os.path.join(hd, 'dillo', 'songs', f'{i:02d}.json'), 'w'))
    rc, out = run_py(os.path.join(REPO, 'claude/tests/dillo-audit/harmony_check.py'),
                     [dump_path, os.path.join(hd, 'dillo'), os.path.join(hd, 'telegram')])
    G['G3c harmony_check.py (catalogue lines, pads, lead only)'] = dict(ok=rc == 0, detail=tail(out, 2))
    rc, out = run_py(os.path.join(REPO, 'claude/telegram/check_pads_lead.py'), [],
                     replace=("os.path.join(REPO, 'tests', 'dillo-audit', 'cat-boot28.json')", repr(dump_path)))
    G['G3d check_pads_lead.py on the dump'] = dict(ok=rc == 0, detail=tail(out, 1))
    rc, out = run_py(os.path.join(REPO, 'claude/tests/drum-audit/check_drums.py'), [dump_path])
    G['G3e check_drums.py, all 21 eras'] = dict(ok=rc == 0, detail=tail(out, 1))

    # G4 field diff
    la, lb = leaves(rc2), leaves(r9)
    changed = sorted(k for k in set(la) | set(lb) if la.get(k) != lb.get(k))
    unexpected = [k for k in changed if not any(re.match(p, k) for p in ALLOWED)]
    groups = collections.Counter(re.sub(r'\[\d+\]', '[]', k).split('.parts[]')[-1].split('[]')[0] if '.parts[]' in re.sub(r'\[\d+\]', '[]', k)
                                 else re.sub(r'\[\d+\]', '[]', k) for k in changed)
    roland_same = rc2['songs'][21:] == r9['songs'][21:] and len(r9['songs']) == len(rc2['songs'])
    top_same = {k: v for k, v in rc2.items() if k not in ('eras', 'songs')} == {k: v for k, v in r9.items() if k not in ('eras', 'songs')}
    per_part = collections.Counter()
    for k in changed:
        m = re.match(r'^\.(eras|songs)\[(\d+)\]\.parts\[(\d+)\]\.([a-z]+)', k)
        if m:
            per_part[(m.group(1), int(m.group(2)), int(m.group(3)), m.group(4))] += 1
    diff = {'changed_leaves': len(changed), 'unexpected': unexpected, 'by_field': dict(groups),
            'roland_songs_identical': roland_same, 'top_level_identical': top_same,
            'parts_changed': sorted({(g, b, p) for g, b, p, _ in per_part}),
            'per_part_fields': [[g, (rc2[g][b].get('id') or rc2[g][b].get('song')), rc2[g][b]['parts'][p]['name'], f, n]
                                for (g, b, p, f), n in sorted(per_part.items())],
            'era_level': sorted(k for k in changed if re.match(r'^\.eras\[\d+\]\.(dillo|swing)', k))}
    json.dump(diff, open(os.path.join(a.out, 'diff-report.json'), 'w'), indent=1, ensure_ascii=False)
    G['G4 nothing else changed'] = dict(ok=not unexpected and roland_same and top_same, detail=(
        f"{len(changed)} changed leaves, all in the allowed set; 138 Roland entries ({len(r9['songs']) - 21} song banks) "
        f"and top level identical; fields: {dict(sorted(groups.items()))}") if not unexpected else unexpected[:20])

    # G5 firmware read paths
    rows, trios = {}, {}
    for tag, inc in (('rc2', rc2_inc), ('r9', r9_inc)):
        blob = bbpc.read_inc(inc)
        bin_path = os.path.join(a.work, tag + '.bbpc')
        open(bin_path, 'wb').write(blob)
        rc, so, se = sh([probe, bin_path], env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
        if rc:
            raise SystemExit('probe failed ' + so[-500:] + se[-2000:])
        open(os.path.join(a.work, tag + '-probe.tsv'), 'w').write(so)
        lines = so.splitlines()
        rows[tag] = list(csv.DictReader(io.StringIO('\n'.join(l for l in lines if not l.startswith('trio\t'))), delimiter='\t'))
        trios[tag] = {l.split('\t')[2]: l.split('\t')[3:] for l in lines if l.startswith('trio\t')}
    worse, new_issue, notes = [], [], []
    for x, y in zip(rows['rc2'], rows['r9']):
        key = f"{x['group']}[{x['bank']}].parts[{x['part']}] {x['name']}"
        for f in ('read', 'rich', 's0', 's1', 'recipe', '808'):
            if x[f] != y[f] and y[f] not in ('0', '1', '-'):
                worse.append(f'{key}: {f} {x[f]} -> {y[f]}')
            if f in ('rich', 's0', 's1', 'read', 'recipe') and y[f] not in ('0', '-'):
                worse.append(f'{key}: {f} = {y[f]}')
            if f == '808' and y[f] != '1':
                worse.append(f'{key}: not admitted on the effective 808 voice set')
        for f in ('issues', 'issues909'):
            if int(y[f]) & ~int(x[f]):
                new_issue.append(f'{key}: {f} {x[f]} -> {y[f]}')
    n_eras = sum(1 for r in rows['r9'] if r['group'] == 'eras')
    G['G5 firmware read paths (rc2 loaders, every section)'] = dict(ok=not worse, detail=(
        f"{len(rows['r9'])} sections ({n_eras} era parts): drum read, rich phrase, 808 voice admission, Dillo + Telegram "
        f"synth data, era recipe all OK; trio/808-grid issue parts {sum(r['group']=='eras' and r['issues']!='0' for r in rows['rc2'])} -> "
        f"{sum(r['group']=='eras' and r['issues']!='0' for r in rows['r9'])}; new projection issues (reported, not on the "
        f"device's rich path): {len(new_issue)}") if not worse else worse[:20], new_projection_issues=new_issue)
    # HOME era load: bb_home_input.inc:39-45 -> bb_performer -> bb_factory_request_trio -> bb_factory_prepare_trio,
    # which refuses the whole era (BB_FTRIO_CONTENT=4) if any part has an 808-grid projection issue
    # (bb_factory_trio.c:53-70). Policy THREE_ENGINE (REC held); policy 0 refuses every era with pads (GREYHOUND=5).
    names = {0: 'OK', 4: 'CONTENT', 5: 'GREYHOUND'}
    lost = [e for e, v in trios['rc2'].items() if v[1] == '0' and trios['r9'][e][1] != '0']
    gained = [e for e, v in trios['rc2'].items() if v[1] != '0' and trios['r9'][e][1] == '0']
    refused = {e: [f"{'ABCDEFGH'[k]}:{x}" for k, x in enumerate(v[3:]) if x.split('/')[1] != '0' or x.split('/')[0] != '0']
               for e, v in trios['r9'].items() if v[1] != '0'}
    G['G5c HOME era load (bb_factory_prepare_trio, REC/THREE_ENGINE)'] = dict(ok=not lost, detail=(
        f"loadable eras rc2 {sum(v[1] == '0' for v in trios['rc2'].values())}/21 -> r9 {sum(v[1] == '0' for v in trios['r9'].values())}/21; "
        f"newly loadable {gained}; newly refused {lost}; refused in r9 (part:drum_result/issues/s0/s1) {refused}"))
    lim = rep['r9']
    G['G5b format limits'] = dict(ok=lim['records'] < 65535 and lim['bytes'] <= 1048576 and lim['metadata'] <= 1048576,
                                  detail=f"records {lim['records']} < 65535; container {lim['bytes']} B <= 1 MiB (BB_PC_MAX_BYTES)")
    # size
    delta = rep['delta_bytes']
    G['G6 size within budget'] = dict(ok=delta <= CODE_FREE_RC2, detail=(
        f"catalogue {rep['base']['bbpc_bytes']} -> {lim['bytes']} B ({delta:+d}); rc2 code free {CODE_FREE_RC2} B of the "
        f"{APP_SLOT} B XIP slot (app.ld) -> about {CODE_FREE_RC2 - delta} B left (HYPOTHESIS until the iMac lane links)"))
    G = {k: v for k, v in G.items()}
    json.dump(G, open(os.path.join(a.out, 'gates.json'), 'w'), indent=1, ensure_ascii=False)
    for k, v in G.items():
        print(('PASS ' if v['ok'] else 'FAIL ') + k + ': ' + (v['detail'] if isinstance(v['detail'], str) else json.dumps(v['detail'])))
    sys.exit(0 if all(v['ok'] for v in G.values()) else 1)


if __name__ == '__main__':
    main()
