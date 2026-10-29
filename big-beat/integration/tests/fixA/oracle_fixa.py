"""Fix A gate: the worker/serial/faults oracle (tests/oracle) built with -DBB_FIXA=1 against a source tree.
Pass = worker digest equals serial digest, faults retire every failed call with 0 refused, and the fix A
counters show kit A's metal really rode the CPU1 job. Usage: python3 -I oracle_fixa.py SRC OUT [-DNAME=VALUE ...]
Extra defines replace the oracle's own setting of that name (e.g. -DBB_WITH_TELEGRAM=1 for Dillo Duo) or are added;
-DBB_FIXA=0 runs the plain oracle with those defines (no fix A requirement)."""
import json, pathlib, subprocess, sys
T = pathlib.Path(__file__).resolve().parent.parent
SRC, OUT = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
EXTRA = sys.argv[3:]
if not any(d.startswith('-DBB_FIXA=') for d in EXTRA): EXTRA.append('-DBB_FIXA=1')
FIXA = '-DBB_FIXA=1' in EXTRA
names = {d.split('=')[0] for d in EXTRA}
OUT.mkdir(parents=True, exist_ok=False)
c_src = OUT / 'integration-480.c'
c_src.write_text((T / 'oracle/integration-480.c').read_text().replace('@SRC@', str(SRC)))
base = [a.replace('@SRC@', str(SRC)).replace('@ORACLE@', str(c_src)) for a in json.loads((T / 'oracle/compile.json').read_text())]
base = [a for a in base if a.split('=')[0] not in names]
if '-DBB_WITH_TELEGRAM=1' in EXTRA: base.append(str(SRC / 'firmware/src/bb/bb_telegram.c'))  # Dillo Duo's lead voice
res = {}
for name, drop, add in (('worker', (), ()), ('serial', (), ('-DFORCE_SERIAL=1',)), ('faults', ('-DAUDIO_ONLY=1',), ())):
    cmd = [a.replace('@OUT@', str(OUT / name)) for a in base if a not in drop] + list(add) + EXTRA
    r = subprocess.run(cmd, capture_output=True, text=True)
    (OUT / f'{name}-compile.log').write_text(r.stdout + r.stderr)
    if r.returncode: print(f'{name}: compile failed'); print(r.stderr[-4000:]); sys.exit(1)
    r = subprocess.run([str(OUT / name)], capture_output=True, text=True, cwd=OUT)
    (OUT / f'{name}.log').write_text(r.stdout + r.stderr)
    if r.returncode: print(f'{name}: exit {r.returncode}'); print(r.stderr[-4000:]); sys.exit(1)
    tail = r.stdout.strip().splitlines()[-2:]
    stats = dict(kv.split('=') for kv in tail[1].split())
    fixa = [l for l in r.stderr.splitlines() if l.startswith('fixa ')]
    res[name] = {'digest': tail[0].split()[0], **{k: stats[k] for k in ('worker_jobs', 'refused', 'failed_calls', 'retirements')},
                 'fixa': dict(kv.split('=') for kv in fixa[-1].split()[1:]) if fixa else {}}
    print(name, json.dumps(res[name]))
ok = (res['worker']['digest'] == res['serial']['digest'] and int(res['worker']['worker_jobs']) > 0 and
      res['faults']['failed_calls'] == res['faults']['retirements'] and res['faults']['refused'] == '0' and
      (not FIXA or int(res['worker']['fixa'].get('jobs', 0)) > 0))
(OUT / 'summary.json').write_text(json.dumps({'passed': ok, 'defines': EXTRA, **res}, indent=1))
print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)
