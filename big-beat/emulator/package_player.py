#!/usr/bin/env python3
"""Stage a matched browser player; does not publish or contact an instrument."""
import argparse, base64, gzip, hashlib, html, json, re, shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from bb_library_identity import verify_library


def package(wasm, library, output, muted=False):
    wasm, library, output = map(Path, (wasm, library, output))
    identity = json.loads((ROOT / 'big-beat/identity.json').read_text())
    receipt = verify_library(library, ROOT)
    binary = wasm.read_bytes()
    # Read identity from the actual module, not its filename or caller's label.
    script = r'''
const fs=require('fs');
(async()=>{const {instance}=await WebAssembly.instantiate(fs.readFileSync(process.argv[1]),{});
const x=instance.exports; if(typeof x.emu_led_state!=='function')throw Error('missing LED-state interface'); const read=p=>{const b=new Uint8Array(x.memory.buffer);let e=p;while(e<b.length&&e-p<80&&b[e])e++;if(e>=b.length||e-p>=80)throw Error('identity bounds');return new TextDecoder().decode(b.subarray(p,e));};
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
    tokens = {'WASM_GZ_B64':base64.b64encode(gzip.compress(binary,mtime=0)).decode(),
              'DISPLAY_HTML':html.escape(identity['display_name']), 'PRODUCT_HTML':html.escape(identity['product']),
              'DISPLAY_JSON':json.dumps(identity['display_name']), 'PRODUCT_JSON':json.dumps(identity['product']),
              'WASM_SHORT':sha[:8], 'LIBRARY_NAME':name, 'LIBRARY_SHA':receipt['sha256'],
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
              'publication_approved':False,'hardware_acceptance':False}
    (output/'PLAYER-RECEIPT.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('wasm');p.add_argument('library');p.add_argument('output');p.add_argument('--muted',action='store_true')
    a=p.parse_args()
    print(json.dumps(package(a.wasm,a.library,a.output,a.muted),indent=2))
