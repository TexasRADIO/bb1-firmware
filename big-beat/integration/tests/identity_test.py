#!/usr/bin/env python3
"""2.1 RC2: the release identity is BB-1 2.1 everywhere it is bound.
The product id follows the rule BB1 has used since 1.5: FM-1_9220 + major (2 digits) + minor (2 digits)
(1.5 -> FM-1_92200105, 2.0 -> FM-1_92200200, so 2.1 -> FM-1_92200201). Checks big-beat/identity.json, the app
header firmware/src/bb_identity.h (through tools/bb_identity.py's own validation), the package-marker rule and every
build recipe that rewrites -DFELUCCA_ID (the string the app carries). usage: identity_test.py SRC"""
import json, sys, importlib.util
sys.dont_write_bytecode = True   # (loads tools/*.py by path: no __pycache__ in the tree)
from pathlib import Path
RELEASE = '2.1'
src = Path(sys.argv[1]).resolve()
checks = failures = 0
def check(ok, what):
    global checks, failures
    checks += 1
    if not ok: failures += 1; print('FAIL:', what)
def product_for(version):
    major, minor = (int(x) for x in version.split('.')[:2])
    return f'FM-1_9220{major:02d}{minor:02d}'
check(product_for('1.5') == 'FM-1_92200105' and product_for('2.0') == 'FM-1_92200200', 'the 1.5 / 2.0 product rule')
ident = json.loads((src / 'big-beat/identity.json').read_text())
check(ident['version'] == RELEASE, f"identity.json version {ident['version']!r}, want {RELEASE!r}")
check(ident['display_name'] == f'BB-1 {RELEASE}', f"display_name {ident['display_name']!r}")
check(ident['product'] == product_for(RELEASE), f"product {ident['product']!r}, want {product_for(RELEASE)!r}")
check(ident['usb_product'] == 'Big Beat', 'USB product name unchanged')
spec = importlib.util.spec_from_file_location('bb_identity', src / 'tools/bb_identity.py')
try:
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)   # validates the header against the json
    check(mod.validate_identity(ident['product']) == ident['product'], 'package marker rule')
except Exception as e:
    check(False, f'tools/bb_identity.py: {e}')
n = 0
for r in sorted((src / 'big-beat/integration/build/recipes').glob('*/recipe.json')):
    for k, v in json.loads(r.read_text()).get('flag_edits', {}).items():
        if k.startswith('-DFELUCCA_ID='):
            n += 1
            check(v == f'-DFELUCCA_ID="{ident["product"]}"', f'{r.parent.name}: {v}')
check(n >= 4, f'{n} recipes bind FELUCCA_ID')
print(f'identity: {ident["display_name"]} {ident["product"]} version {ident["version"]}, {n} recipes, {checks} checks, {failures} failures')
sys.exit(failures != 0)
