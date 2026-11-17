#!/usr/bin/env python3
"""Stage a matched browser player; does not publish or contact an instrument."""
import argparse, base64, gzip, hashlib, html, json, re, shutil, struct, subprocess, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from bb_library_identity import verify_library


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bundle_pins(app, release_package, library):
    if bool(app) != bool(release_package):
        raise ValueError('bundle import needs both app and release package')
    if not app:
        return None
    app, release_package = map(Path, (app, release_package))
    if not app.is_file() or not release_package.is_file():
        raise ValueError('bundle dependency input missing')
    identity = json.loads((ROOT / 'big-beat/identity.json').read_text())
    # Decode the package with the installer parser. Hash the same bytes that
    # were compared so the receipt never binds an unchecked application pair.
    script = r'''
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const [parser, appPath, packagePath, product] = process.argv.slice(1);
const {Package, crc16} = await import(parser);
const app = readFileSync(appPath), firmware = readFileSync(packagePath);
const pkg = new Package(firmware);
if (pkg.product !== product || !app.includes(Buffer.from(product + '\0')))
    throw new Error('application or firmware package product does not match BB1');
const slot = pkg.dec.subarray(pkg.appOff, pkg.appOff + pkg.app.size);
if (crc16(slot) !== pkg.app.dcrc) throw new Error('app.bin CRC mismatch');
if (app.length > slot.length || !app.equals(slot.subarray(0, app.length)) ||
    slot.subarray(app.length).some(byte => byte !== 0xff))
    throw new Error('application does not match app.bin in the firmware package');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
console.log(JSON.stringify({application_sha256:sha(app), application_bytes:app.length,
    release_package_sha256:sha(firmware), release_package_bytes:firmware.length}));
'''
    checked = subprocess.run(
        ['node', '--input-type=module', '-e', script, (ROOT / 'big-beat/installer/fm1pkg.js').as_uri(),
         str(app.resolve()), str(release_package.resolve()), identity['product']],
        capture_output=True, text=True)
    if checked.returncode:
        error = re.search(r'^Error: (.+)$', checked.stderr, re.M)
        raise ValueError(error[1] if error else 'could not verify the application and firmware package')
    firmware_pins = json.loads(checked.stdout)
    library_spec = json.loads((ROOT / 'big-beat/sound-library.json').read_text())
    catalogue = (ROOT / 'big-beat/catalogue/catalogue.json').read_bytes()
    text = (ROOT / 'firmware/src/bb/bb_performance_catalogue.inc').read_text()
    payload = text.split('{', 1)[1].split('};', 1)[0]
    compiled = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', payload))
    if len(compiled) < 16 or compiled[:4] != b'BBPC':
        raise ValueError('compiled catalogue framing')
    size, crc = struct.unpack_from('<II', compiled, 8)
    if size != len(compiled) or zlib.crc32(compiled[:12] + compiled[16:]) != crc:
        raise ValueError('compiled catalogue checksum')
    return {'format':'BB1_PROJECT_BUNDLE','schema':1,
            'identity':{k:identity[k] for k in ('display_name','product','version','usb_product')},
            **firmware_pins,
            'sound_library_sha256':sha(library),'sound_library_bytes':Path(library).stat().st_size,
            'sound_library_name':library_spec['name'],
            'catalogue_source_sha256':hashlib.sha256(catalogue).hexdigest(),'catalogue_source_bytes':len(catalogue),
            'catalogue_runtime_crc32':f'{crc:08x}','catalogue_runtime_stamp':0x42504331}


def package(wasm, library, output, muted=False, app=None, release_package=None):
    wasm, library, output = map(Path, (wasm, library, output))
    identity = json.loads((ROOT / 'big-beat/identity.json').read_text())
    profile_path = wasm.parent / 'BUILD-PROFILE.json'
    profile = json.loads(profile_path.read_text()) if profile_path.is_file() else None
    candidate_only = bool(profile and profile.get('candidate_defines'))
    receipt = verify_library(library, ROOT)
    binary = wasm.read_bytes()
    pins = bundle_pins(app, release_package, library)
    flags = (profile or {}).get('flags', [])
    step_conditions = '-DBB_WITH_STEP_CONDITIONS=1' in flags
    project_wire_max = 3 if step_conditions else 2
    if pins is not None:
        pins['project_wire_max'] = project_wire_max
    # Read identity from the actual module, not its filename or caller's label.
    script = r'''
const fs=require('fs');
(async()=>{const {instance}=await WebAssembly.instantiate(fs.readFileSync(process.argv[1]),{});
const x=instance.exports;
if(typeof x.emu_led_state!=='function')throw Error('missing LED-state interface');
if(typeof x.emu_project_can_begin!=='function')throw Error('missing project readiness interface');
if(typeof x.emu_project_epoch!=='function'||typeof x.emu_project_arrangement_revision!=='function')
  throw Error('missing project revision interface');
if(typeof x.emu_enable_drum_b!=='function')throw Error('missing Drum B enable interface');
const read=p=>{const b=new Uint8Array(x.memory.buffer);let e=p;while(e<b.length&&e-p<80&&b[e])e++;if(e>=b.length||e-p>=80)throw Error('identity bounds');return new TextDecoder().decode(b.subarray(p,e));};
console.log(JSON.stringify({product:read(x.emu_product_id()),display_name:read(x.emu_display_name())}));})().catch(e=>{console.error(e);process.exit(1)});
'''
    actual = json.loads(subprocess.check_output(['node', '-e', script, str(wasm.resolve())], text=True))
    if any(actual[k] != identity[k] for k in ('product', 'display_name')):
        raise ValueError('module identity differs from release identity')
    if output.exists():
        raise ValueError('use a new output directory')
    sha = hashlib.sha256(binary).hexdigest()
    template = (ROOT / 'big-beat/emulator/player.html.in').read_text()
    name = json.loads((ROOT / 'big-beat/sound-library.json').read_text())['name']
    if Path(name).name != name or not re.fullmatch(r'[A-Za-z0-9._-]+', name):
        raise ValueError('invalid library output name')
    receipt['output_name'] = name
    candidate_features=', '.join(f'{k}={v}' for k,v in sorted((profile or {}).get('candidate_defines',{}).items()))
    candidate_banner=(f'<aside class="candidate" role="status">Experimental candidate · not a public release · '
                      f'{html.escape(candidate_features)}</aside>') if candidate_only else ''
    condition_help = (
        '<li><b>Step conditions.</b> In Big Beat Steps, hold a lit white key and turn <b>K3</b>. '
        'On a computer, hold the matching keyboard key and scroll over K3. '
        'On a touchscreen, hold the step with one finger and turn K3 with another. '
        'The step shows 1 for the first loop, 2 for every second loop, 4 for every fourth loop, '
        'or F for Fill. Return to the Big Beat pads and hold black key <b>B5</b> to play Fill steps. '
        'Projects saved by this build need a player with step conditions enabled.</li>'
        if step_conditions else ''
    )
    tokens = {'WASM_GZ_B64':base64.b64encode(gzip.compress(binary,mtime=0)).decode(),
              'DISPLAY_HTML':html.escape(identity['display_name']), 'PRODUCT_HTML':html.escape(identity['product']),
              'DISPLAY_JSON':json.dumps(identity['display_name']), 'PRODUCT_JSON':json.dumps(identity['product']),
              'WASM_SHORT':sha[:8], 'LIBRARY_NAME':name, 'LIBRARY_SHA':receipt['sha256'],
              'CANDIDATE_BANNER':candidate_banner, 'STEP_CONDITION_HELP':condition_help,
              'BUNDLE_PINS_JSON':json.dumps(pins,separators=(',',':')),
              'PROJECT_WIRE_MAX':str(project_wire_max),
              'MASTER':'0' if muted else '0.8'}
    for key, value in tokens.items():
        token='@'+key+'@'
        if token not in template: raise ValueError('missing template token '+token)
        template=template.replace(token,value)
    if re.search(r'@[A-Z_]+@',template): raise ValueError('unexpanded template token')
    output.mkdir(parents=True)
    (output/'index.html').write_text(template)
    shutil.copyfile(library,output/name)
    manifest={'identity':actual,'wasm_sha256':sha,'wasm_bytes':len(binary),'library':receipt,
              'html_sha256':hashlib.sha256(template.encode()).hexdigest(),'muted':muted,
              'build_profile':profile,'candidate_only':candidate_only,'bundle_import_pins':pins,
              'publication_approved':False,'hardware_acceptance':False}
    (output/'PLAYER-RECEIPT.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if profile is not None:
        (output/'BUILD-PROFILE.json').write_text(json.dumps(profile,indent=2)+'\n')
    return manifest

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('wasm');p.add_argument('library');p.add_argument('output');p.add_argument('--muted',action='store_true')
    p.add_argument('--app');p.add_argument('--release-package')
    a=p.parse_args()
    print(json.dumps(package(a.wasm,a.library,a.output,a.muted,a.app,a.release_package),indent=2))
