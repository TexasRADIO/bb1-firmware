#!/usr/bin/env python3
"""r9 catalogue per-section audio accounting (host only). Run after pack.py and gates.py:

    python3 claude-code/r9/catalogue/audio_account.py [--out claude-code/r9/catalogue/out]

Fixture: claude-code/timing/tests/integration (342 sections x two native 808 kits + Dillo + echo/spring, worker
failures, STOP tails), built with its retained command line against rc2's tree, with three changes made to a copy:
  1. the section count is the catalogue's (rc2 and r9 have 480 sections: 342 + 138 Roland) instead of 342;
  2. section s's Dillo plays section s's own catalogue line and expression (bb_factory_prepare_synth_data, synth 0),
     instead of a synthetic pattern;
  3. for an era section, its era recipe's Dillo patch (bb_factory_read_recipe) is applied, so preset changes sound.
Section s renders the drum phrases of sections s and (7s+3)%N, then (s+1)%N and (s+171)%N (the fixture's own pairing).
The fixture is built twice, with rc2's and with r9's bb_performance_catalogue.inc, and per-section digests compared.
Prediction from the probe (gates.py work dir): s changes iff the rich phrase (rich_ix digest) of any of its four drum
sections changed, or its own Dillo data (s0_data) or era recipe (recipe_data) changed. Gate: measured == predicted.
Telegram is compiled out (BB_WITH_TELEGRAM=0) and the firmware never reads part greyhound data, so pad/lead changes
are inaudible in rc2 by construction (and none differ from rc2 anyway).
"""
import argparse, csv, hashlib, importlib.util, io, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
TIMING = os.path.join(REPO, 'claude-code', 'timing')


def fixture_source():
    s = open(os.path.join(TIMING, 'tests', 'integration', 'test.c')).read()
    reps = [
        ('#include "bb_browser.h"\n', '#include "bb_browser.h"\n#include "bb_factory_pattern.h"\n#include "bb_factory_recipe.h"\n'),
        ('static bb_phrase_index catalogue[342];static unsigned sections;',
         'static bb_phrase_index catalogue[512];static bb_factory_choice choices[512];static unsigned sections,N;'),
        ('assert(sections<342);', 'assert(sections<512);choices[sections]=choice;'),
        ('assert(sections==342);', 'N=sections;assert(N>=342);'),
        ('for(unsigned s=0;s<342;s++){', 'for(unsigned s=0;s<N;s++){'),
        ('(s*7u+3u)%342u', '(s*7u+3u)%N'), ('(s+1u)%342u', '(s+1u)%N'), ('(s+171u)%342u', '(s+171u)%N'),
        ('''  q->notes[i].accent=(uint8_t)((i%4)==0);q->notes[i].slide=(uint8_t)((i%7)==3);}''',
         '''  q->notes[i].accent=(uint8_t)((i%4)==0);q->notes[i].slide=(uint8_t)((i%7)==3);}
 /* r9 accounting: the section's own catalogue Dillo line, expression and (eras) recipe patch. */
 {static bb_pattern_data pd;assert(bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[s],0,&pd)==BB_FACTORY_PATTERN_OK);
  memcpy(q->notes,pd.notes,sizeof q->notes);q->length=pd.length;q->next=0;
  memcpy(audio.patterns.expression[0],pd.expression,sizeof audio.patterns.expression[0]);
  if(choices[s].group==BB_BROWSER_ERAS){static bb_factory_recipe r;
   assert(bb_factory_read_recipe(&bb_factory_catalogue,&choices[s],&r)==BB_RECIPE_OK);audio.core.dillo=r.dillo;}}'''),
    ]
    for a, b in reps:
        if a not in s:
            raise SystemExit('fixture changed upstream; cannot adapt: ' + a[:60])
        s = s.replace(a, b)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, 'out'))
    a = ap.parse_args()
    work = os.path.join(a.out, 'work')
    spec = importlib.util.spec_from_file_location('timing', os.path.join(TIMING, 'run-host-tests.py'))
    tm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tm)
    src = fixture_source()
    fx = os.path.join(work, 'account-fixture')
    os.makedirs(os.path.join(fx, 'tests', 'integration'), exist_ok=True)
    open(os.path.join(fx, 'tests', 'integration', 'test.c'), 'w').write(src)
    cmd = json.load(open(os.path.join(TIMING, 'tests', 'integration', 'command.json')))
    srcs = [x for x in cmd if x.endswith('.c')]
    extra = []
    for f in ('bb_factory_pattern.c', 'bb_note_expression_catalogue.c', 'bb_factory_recipe.c'):
        if not any(x.endswith('/bb/' + f) for x in srcs):
            extra.append(tm.OLD + '/firmware/src/bb/' + f)
    i = cmd.index('-o')
    json.dump(cmd[:i] + extra + cmd[i:], open(os.path.join(fx, 'tests', 'integration', 'command.json'), 'w'))
    tm.HERE = type(tm.HERE)(fx)
    trees = {'rc2': os.path.join(work, 'rc2')}
    r9tree = os.path.join(work, 'rc2-with-r9')
    if os.path.isdir(r9tree):
        shutil.rmtree(r9tree)
    shutil.copytree(trees['rc2'], r9tree, symlinks=True)
    shutil.copy(os.path.join(a.out, 'bb_performance_catalogue.inc'), os.path.join(r9tree, 'firmware/src/bb/bb_performance_catalogue.inc'))
    trees['r9'] = r9tree
    res = {}
    for tag, tree in trees.items():
        bwork = os.path.join(work, 'account-build')
        d = tm.build(f'integration-account-{tag}', type(tm.HERE)(tree), 'integration', type(tm.HERE)(bwork))
        so, t = tm.run(d, {'ASAN_OPTIONS': 'detect_leaks=0'})
        se = open(os.path.join(d, 'stderr.log')).read()
        dig = dict(re.findall(r'section (\d+) digest ([0-9a-f]+)', se))
        res[tag] = {'summary': so.strip().splitlines()[-1], 'stream': so.strip().splitlines()[0].split()[0],
                    'digests': {int(k): v for k, v in dig.items()}, 'seconds': t}
        print(tag, t, 's', res[tag]['summary'][:200], flush=True)
    # probe.c also prints one 'trio' row per era (HOME era load, G5c); those are not sections
    rows = {t: list(csv.DictReader(io.StringIO(''.join(l for l in open(os.path.join(work, f'{t}-probe.tsv'))
                                                       if not l.startswith('trio\t'))), delimiter='\t'))
            for t in ('rc2', 'r9')}
    n = len(rows['r9'])
    assert n == len(res['r9']['digests']) == len(res['rc2']['digests']), 'section count mismatch'
    def chg(i, f):
        return rows['rc2'][i][f] != rows['r9'][i][f]
    predicted, why = set(), {}
    for s in range(n):
        drums = {s, (7 * s + 3) % n, (s + 1) % n, (s + 171) % n}
        r = [f'drums of {i}' for i in sorted(drums) if chg(i, 'rich_ix')]
        if chg(s, 's0_data'):
            r.append('own Dillo')
        if rows['r9'][s]['group'] == 'eras' and chg(s, 'recipe_data'):
            r.append('own era preset')
        if r:
            predicted.add(s)
            why[s] = r
    measured = {s for s in range(n) if res['rc2']['digests'][s] != res['r9']['digests'][s]}
    label = lambda s: f"{s} {rows['r9'][s]['group']}[{rows['r9'][s]['bank']}].{rows['r9'][s]['part']} {rows['r9'][s]['name']}"
    out = {'sections': n, 'changed_measured': len(measured), 'changed_predicted': len(predicted),
           'unexplained_changes': [label(s) for s in sorted(measured - predicted)],
           'predicted_but_unchanged': [label(s) for s in sorted(predicted - measured)],
           'unchanged_sections': n - len(measured),
           'rc2': {k: v for k, v in res['rc2'].items() if k != 'digests'},
           'r9': {k: v for k, v in res['r9'].items() if k != 'digests'},
           'changed': [[label(s), why.get(s, [])] for s in sorted(measured)],
           'ok': measured <= predicted}
    json.dump(out, open(os.path.join(a.out, 'audio-accounting.json'), 'w'), indent=1)
    print(json.dumps({k: out[k] for k in ('sections', 'changed_measured', 'changed_predicted', 'unchanged_sections',
                                          'unexplained_changes', 'ok')}))
    print('predicted but unchanged:', len(out['predicted_but_unchanged']), out['predicted_but_unchanged'][:12])
    sys.exit(0 if out['ok'] else 1)


if __name__ == '__main__':
    main()
