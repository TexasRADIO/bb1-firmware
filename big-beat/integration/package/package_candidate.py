#!/usr/bin/env python3
"""Package a from-source candidate build as an FM-1 .fwsc (offline; never contacts the unit).

Same procedure as boot42's verification/package-experimental.py: the release-dependency audit's
pinned loader, packer, verifier and SDK payloads (all hash-checked), decode-and-verify of the
written package, eight negative cases, and the installed boot42 package recorded as rollback.
Writes only the sound-library-free firmware package: no asset or user-data region is touched.
usage: package_candidate.py BUILD_DIR OUT_DIR NAME"""
from pathlib import Path
import ast, hashlib, importlib.util, json, struct, sys, types

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bb_identity import PRODUCT, DISPLAY_NAME, require_compiled_identity, verifier_for_product

build, outdir, name = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
digest = lambda data: hashlib.sha256(data).hexdigest()
sha = lambda path: digest(Path(path).read_bytes())
report = json.loads((build / 'report.json').read_text())
link = report['candidate']
image = build / 'candidate' / 'app.bin'
app = image.read_bytes()
require_compiled_identity(app)
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
verify = verifier_for_product(verifier_path)
tree = ast.parse((tools / 'target/tools/fm1_install.py').read_text())
nodes = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in ('InstallError', 'product_of', 'logical_image')]
assert len(nodes) == 3
ns = {'BLOCKS': 20, 'BLK': 0x30, 'KEEP': 0x2f}
exec(compile(ast.Module(body=nodes, type_ignores=[]), 'pure_decoders', 'exec'), ns)
decoder = types.SimpleNamespace(**ns)

raw = packer.ufw(packer.flash_image(app, packer.KEY), loader, PRODUCT)
decoded = verify(raw, app, loader, sdk, packer, decoder)
assert decoded['app_and_loader_exact'] and decoded['container_and_nested_crc_checks']
assert decoded['padding_bytes'] == link['code_free']
assert raw == packer.ufw(packer.flash_image(app, packer.KEY), loader, PRODUCT)        # deterministic

def reject(label, data):
    try: verify(data, app, loader, sdk, packer, decoder)
    except (AssertionError, struct.error, UnicodeDecodeError, ns['InstallError']): return label
    raise AssertionError('verifier accepted ' + label)
negative = [reject('truncated', raw[:-1]), reject('suffix', raw + b'\0')]
for off in (0, 47, 100, 2000, len(raw) - 1):
    bad = bytearray(raw); bad[off] ^= 1; negative.append(reject('corrupt-' + str(off), bytes(bad)))
negative.append(reject('wrong-product', packer.ufw(packer.flash_image(app, packer.KEY), loader, 'OTHER_900')))
assert len(negative) == 8

rollback = OB / 'FM1 Recovery Vault/1.5-c04aa9f-candidate-20261029/bb1-1.5.fwsc'
assert sha(rollback) == '2ef5f0112657e25fcce3f25d3e9be6eb6ba1832d941386cbf3543a450686dcfc'      # preserved exact original public1.5 rollback
outdir.mkdir(parents=True, exist_ok=True)
package = outdir / (name + '.fwsc')
assert not package.exists(), package
package.write_bytes(raw)
assert verify(package.read_bytes(), app, loader, sdk, packer, decoder) == decoded
out = {'packaged': True, 'product': PRODUCT, 'display_name': DISPLAY_NAME, 'installed': False, 'hardware_tested': False,
       'asset_write': False, 'user_data_write': False, 'app_sha256': link['sha256'], 'app_bytes': link['bytes'],
       'fit': {k: link[k] for k in ('code_free', 'ram_free', 'pool_free', 'ram_text_bytes')},
       'package': {'path': str(package), 'sha256': sha(package), 'bytes': len(raw)},
       'loader_sha256': sha(loader_path), 'rollback': {'what': 'Original public1.5 retained for rollback; identity-only supplement does not replace its tag', 'path': str(rollback), 'sha256': sha(rollback),
       'app_sha256': 'c3a703a388e8c274585b27097a888d30b3c4f2b92c57e1077cee9e4ebf0d3380'},
       'sound_library_expected_sha256': '154ef6b92553af5803c3e003dc5d2c379aed4e0503f24f08ad8b024cff7d821e',
       'decoded': decoded, 'negative_cases': negative}
(outdir / (name + '-package-report.json')).write_text(json.dumps(out, indent=2) + '\n')
print(json.dumps({k: out[k] for k in ('package', 'app_sha256', 'fit', 'negative_cases')}, indent=2))
