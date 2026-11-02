#!/usr/bin/env python3
"""Compare the substitutions the user-kit resolver shows on screen with the frozen routes
table of PR #68 d097e15 (routes.csv), for the machines installed in the test.
A difference is classed PACK when routes.csv plays that voice natively (substituted=False):
the d097e15 table was generated from the later seven-kit pack, which has sounds the offline
classic-three test pack lacks. Any other difference fails.
usage: check_routes.py uk.out routes.csv"""
import csv, sys
got = {tuple(l[6:].strip().split(',')) for l in open(sys.argv[1]) if l.startswith('ROUTE ')}
kits = {k for k, _, _ in got}
rows = [r for r in csv.DictReader(open(sys.argv[2])) if r['kit'] in kits and r['requested'] != 'Shaker']
want = {(r['kit'], r['requested'], r['played']) for r in rows if r['substituted'] == 'True'}
native = {(r['kit'], r['requested']) for r in rows if r['substituted'] == 'False'}
missing, extra = want - got, got - want
pack = {e for e in extra if (e[0], e[1]) in native}
bad = (extra - pack) | missing
print(f"routes: {len(got)} shown by the resolver for {sorted(kits)}; {len(want)} substituted rows in routes.csv (shaker excluded)")
print(f"routes: {len(want & got)} identical; {len(pack)} PACK differences (voice native in the d097e15 pack, absent from the test pack):")
for e in sorted(pack): print('  PACK', e)
for b in sorted(bad): print('  DIFFER', b)
print('routes: every d097e15 substitution reproduced; no unexplained difference' if not bad else 'routes: DIFFER')
sys.exit(1 if bad else 0)
