#!/usr/bin/env python3
"""Package a from-source candidate build as an FM-1 .fwsc (offline; never contacts the unit).

Same procedure as boot42's verification/package-experimental.py: the release-dependency audit's
pinned loader, packer, verifier and SDK payloads (all hash-checked), decode-and-verify of the
written package, eight negative cases, and the installed boot42 package recorded as rollback.
Writes only the sound-library-free firmware package: no asset or user-data region is touched.
usage: package_candidate.py BUILD_DIR OUT_DIR NAME"""
from pathlib import Path
import ast, hashlib, importlib.util, json, struct, sys, types

build, outdir, name = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
digest = lambda data: hashlib.sha256(data).hexdigest()
sha = lambda path: digest(Path(path).read_bytes())
report = json.loads((build / 'report.json').read_text())
link = report['candidate']
image = build / 'candidate' / 'app.bin'
app = image.read_bytes()
assert sha(image) == link['sha256'] and len(app) == link['bytes']
assert report['resource_limits_pass'] and link['code_free'] >= 0 and link['ram_free'] >= 4096 and link['pool_free'] >= 8192

OB = Path('/Volumes/Orange Base')
deps = json.loads((OB / 'Codex Workspaces/big-beat-fm1-architecture-20261015/experiments/performance-control-contract/'
                        'release_dependencies-r2/report.json').read_text())
assert deps['offline_audit_passed']
loader_path = Path(deps['inputs']['loader']['path']); loader = loader_path.read_bytes()
assert sha(loader_path) == deps['inputs']['loader']['sha256']
tools = OB / 'Codex Workspaces/big-beat-fm1-architecture-20261015/experiments/bb-control-feedback-20261016/package-r1'
packer_path, verifier_path = tools / 'target/tools/fm1pkg_make.py', tools / 'verify_payload.py'
for p in (packer_path, verifier_path):
    assert sha(p) == deps['tools_sha256'][str(p)], p

def load(n, p):
    spec = importlib.util.spec_from_file_location(n, p); m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m); return m
packer = load('bb_packer', packer_path)
packer.SDK = OB / 'Codex Workspaces/fm1-build-setup/ac79-sdk'
sdk = {n: packer.sdk_file(n) for n in deps['sdk_sha256']}
for n, h in deps['sdk_sha256'].items():
    assert digest(sdk[n]) == h, n
verify = load('bb_verifier', verifier_path).verify
tree = ast.parse((tools / 'target/tools/fm1_install.py').read_text())
nodes = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in ('InstallError', 'product_of', 'logical_image')]
assert len(nodes) == 3
ns = {'BLOCKS': 20, 'BLK': 0x30, 'KEEP': 0x2f}
exec(compile(ast.Module(body=nodes, type_ignores=[]), 'pure_decoders', 'exec'), ns)
decoder = types.SimpleNamespace(**ns)

raw = packer.ufw(packer.flash_image(app, packer.KEY), loader, 'FM-1_900')
decoded = verify(raw, app, loader, sdk, packer, decoder)
assert decoded['app_and_loader_exact'] and decoded['container_and_nested_crc_checks']
assert decoded['padding_bytes'] == link['code_free']
assert raw == packer.ufw(packer.flash_image(app, packer.KEY), loader, 'FM-1_900')        # deterministic

def reject(label, data):
    try: verify(data, app, loader, sdk, packer, decoder)
    except (AssertionError, struct.error, UnicodeDecodeError, ns['InstallError']): return label
    raise AssertionError('verifier accepted ' + label)
negative = [reject('truncated', raw[:-1]), reject('suffix', raw + b'\0')]
for off in (0, 47, 100, 2000, len(raw) - 1):
    bad = bytearray(raw); bad[off] ^= 1; negative.append(reject('corrupt-' + str(off), bytes(bad)))
negative.append(reject('wrong-product', packer.ufw(packer.flash_image(app, packer.KEY), loader, 'OTHER_900')))
assert len(negative) == 8

rollback = OB / 'Codex Working Cache/tmp/bb-silent-b-candidate-r1/verification/target-r1/Big-Beat-Studio-HOME-silent-B-EXPERIMENTAL.fwsc'
assert sha(rollback) == 'f3383921a500e4c0d771adcab968b6f905d278ab79b5edc616cdc5e921e5865b'      # installed boot42
outdir.mkdir(parents=True, exist_ok=True)
package = outdir / (name + '.fwsc')
assert not package.exists(), package
package.write_bytes(raw)
assert verify(package.read_bytes(), app, loader, sdk, packer, decoder) == decoded
out = {'packaged': True, 'product': 'FM-1_900', 'installed': False, 'hardware_tested': False,
       'asset_write': False, 'user_data_write': False, 'app_sha256': link['sha256'], 'app_bytes': link['bytes'],
       'fit': {k: link[k] for k in ('code_free', 'ram_free', 'pool_free', 'ram_text_bytes')},
       'package': {'path': str(package), 'sha256': sha(package), 'bytes': len(raw)},
       'loader_sha256': sha(loader_path), 'rollback': {'what': 'installed boot42 package', 'path': str(rollback), 'sha256': sha(rollback),
       'app_sha256': '27e6c75b3508c4df89042481f2425f109f27ce0fd63a06f28499dc29efe44982'},
       'sound_library_expected_sha256': 'f4bbf1a409d8aadb3429232e49dfb06b121070532787d31078db9630b6e7efcc',
       'decoded': decoded, 'negative_cases': negative}
(outdir / (name + '-package-report.json')).write_text(json.dumps(out, indent=2) + '\n')
print(json.dumps({k: out[k] for k in ('package', 'app_sha256', 'fit', 'negative_cases')}, indent=2))
