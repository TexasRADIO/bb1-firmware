"""Exercise installer generation with a real reviewed FWSC and library; no device access."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main(package, library, archive):
    package, library = Path(package).resolve(), Path(library).resolve()
    archive = Path(archive).resolve()
    def entry(name, path):
        data = path.read_bytes()
        return dict(name=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    manifest = dict(version='2.1', product='FM-1_92200201', displayName='BB-1 2.1',
        startingProducts=['FM-1_92200105', 'FM-1_92200200', 'FM-1_92200201'],
        files=[entry('bb1-2.1.fwsc', package), entry('bb1-sounds-2.1.bin', library),
               entry('bb1-source-2.1.tar', archive)])
    script = Path(__file__).with_name('make_installer.py')
    results = []
    with tempfile.TemporaryDirectory(prefix='bb1-installer-') as directory:
        root = Path(directory)
        for case in ['valid', 'missing-policy', 'duplicate-policy', 'wrong-hash', 'wrong-product', 'wrong-library', 'missing-source', 'wrong-source']:
            meta = json.loads(json.dumps(manifest)); sound = library
            if case == 'missing-policy': del meta['startingProducts']
            if case == 'duplicate-policy': meta['startingProducts'] *= 2
            if case == 'wrong-hash': meta['files'][0]['sha256'] = '0' * 64
            if case == 'wrong-product': meta['product'] = 'FM-1_92200200'
            if case == 'wrong-source': meta['files'][2]['sha256'] = '0' * 64
            if case == 'wrong-library':
                sound = root / 'altered.bin'; data = bytearray(library.read_bytes()); data[100] ^= 1
                sound.write_bytes(data)
                # A self-consistent manifest must not override the firmware's library pin.
                meta['files'][1] = entry('bb1-sounds-2.1.bin', sound)
            mf = root / (case + '.json'); mf.write_text(json.dumps(meta)); out = root / case
            cmd = [sys.executable, str(script), str(package), str(sound), '2.1', str(out), '--manifest', str(mf)]
            if case != 'missing-source': cmd += ['--source-archive', str(archive)]
            run = subprocess.run(cmd, capture_output=True, text=True)
            assert (run.returncode == 0) == (case == 'valid'), (case, run.stderr)
            if case != 'valid': assert not out.exists(), case
            else:
                html = (out / 'index.html').read_text()
                assert 'BB1 does not lock the FM-1.' in html
                assert 'Installer files checked. Nothing has been written yet.' in html
                assert 'BB1 2.1, ready.' not in html
                assert '<details id="return-official" open>' in html
                assert 'Choosing the file only checks it. Nothing is written until you press Return to official V15 and confirm.' in html
                for name in ['backup.html', 'compatibility.html']:
                    assert (out / name).read_bytes() == (script.parent / name).read_bytes()
                for name, src in [('bb1-2.1.fwsc', package), ('bb1-sounds-2.1.bin', library), ('bb1-source-2.1.tar', archive)]:
                    assert (out / 'files' / name).read_bytes() == src.read_bytes()
                assert subprocess.run(cmd, capture_output=True).returncode == 0
                frozen = out / 'files/bb1-2.1.fwsc'; frozen.write_bytes(b'existing frozen artifact')
                before = {p.relative_to(out): p.read_bytes() for p in out.rglob('*') if p.is_file()}
                assert subprocess.run(cmd, capture_output=True).returncode != 0
                assert before == {p.relative_to(out): p.read_bytes() for p in out.rglob('*') if p.is_file()}
            results.append(dict(case=case, passed=True))
    print(json.dumps(dict(cases=results, idempotence=True, frozen_artifact_preserved=True, device_access=False), indent=2))


if __name__ == '__main__':
    main(*sys.argv[1:])
