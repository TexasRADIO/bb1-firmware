#!/usr/bin/env python3
"""Fail if a written hardware handoff (HANDOFF-*.md) disagrees with HANDOFF.json on the source tree,
app or package identity. Added after HANDOFF-R9.md swapped the tree and package hashes (#4, 6455318381).
usage (from the review repo root): python3 -I claude-code/integration/tests/check_handoff_doc.py HANDOFF.json DOC.md"""
import json, re, sys
d = json.load(open(sys.argv[1])); s = open(sys.argv[2]).read()
want = {'Source tree': d['source']['expected_tree_sha256'], 'App': d['artifacts']['application']['sha256'],
        'Package': d['artifacts']['package']['sha256']}
bad = []
for label, h in want.items():
    m = re.search(rf'^\| {label} \| `([0-9a-f]{{64}})`', s, re.M)
    if not m or m[1] != h: bad.append(f'{label}: doc {m[1] if m else "missing"} != manifest {h}')
for short in set(re.findall(r'`([0-9a-f]{8})…`', s)):          # every abbreviated hash must name one manifest identity
    hits = [k for k, h in want.items() if h.startswith(short)]
    if 'install of `' + short in s and hits != ['Package']: bad.append(f'install names {short}, not the package')
import os                                                       # every script the doc tells you to run must exist
for path in set(re.findall(r'`(?:python3 (?:-I )?)?((?:claude-code|engineering)/[\w./-]+\.(?:py|sh|mjs))', s)):
    if not os.path.isfile(path): bad.append(f'doc names {path}, which is not in this checkout')
print('\n'.join(bad) or f'{sys.argv[2]}: tree, app and package match {sys.argv[1]}'); sys.exit(1 if bad else 0)
