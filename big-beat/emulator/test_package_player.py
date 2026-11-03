#!/usr/bin/env python3
"""Exercise packaging with actual WASM/library inputs, including refusal before output."""
import base64, gzip, hashlib, json, re, sys, tempfile
from pathlib import Path
from package_player import package

wasm, library = map(Path, sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix='bb1-player-test-') as temp:
    root=Path(temp)
    a=package(wasm,library,root/'a',True)
    b=package(wasm,library,root/'b',True)
    page=(root/'a/index.html').read_text()
    assert (root/'a/index.html').read_bytes()==(root/'b/index.html').read_bytes()
    embedded=re.search(r'const WASM_GZ_B64 = "([^"]+)";',page).group(1)
    assert gzip.decompress(base64.b64decode(embedded))==wasm.read_bytes()
    assert (root/'a'/library.name).read_bytes()==library.read_bytes()
    assert 'masterLevel = 0;' in page
    assert '31 SLOOP kits' not in page and 'BB1 2.0' not in page
    assert '/fm1/brand.css' not in page and '@WASM_GZ_B64@' not in page
    corrupt=root/'bad-library.bin';data=bytearray(library.read_bytes());data[0]^=1;corrupt.write_bytes(data)
    try: package(wasm,corrupt,root/'bad-library-output')
    except ValueError: pass
    else: raise AssertionError('corrupt library accepted')
    assert not (root/'bad-library-output').exists()
    wrong=root/'wrong-id.wasm';data=wasm.read_bytes();old=b'FM-1_92200201';assert old in data
    wrong.write_bytes(data.replace(old,b'FM-1_92200200'))
    try: package(wrong,library,root/'wrong-id-output')
    except ValueError: pass
    else: raise AssertionError('wrong product accepted')
    assert not (root/'wrong-id-output').exists()
    try: package(wasm,library,root/'a')
    except ValueError: pass
    else: raise AssertionError('existing output overwritten')
    assert hashlib.sha256((root/'a/index.html').read_bytes()).hexdigest()==a['html_sha256']
    print(json.dumps({'status':'PASS','deterministic':True,'embedded_wasm_exact':True,'library_exact':True,'corrupt_library_refused':True,'wrong_product_refused':True,'existing_output_preserved':True}))
