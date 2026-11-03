#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the BB1 web installer (after Felucca's web/make_site.py).

  python3 installer/make_installer.py PACKAGE.fwsc SOUNDS.bin VERSION [OUT_DIR] [--manifest FILE]

OUT_DIR (default public/fm1/install) gets:
  index.html            index.tpl.html with the release's identity pinned (sizes and SHA-256)
  fm1ota.js fm1pkg.js fm1backup.js   Felucca's installer modules (GPL-3.0, Leo Kuroshita / Hugelton Instruments)
  bb1sounds.js          the BB1 sound-library step
  files/bb1-VERSION.fwsc, files/bb1-sounds-VERSION.bin   the release files (not in Git: see .gitignore)

The page refuses to install anything whose size or SHA-256 differs from what is pinned here.
"""
import argparse, hashlib, json, re, shutil, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SOUND_BYTES = 245760


def product_of(raw):
    """the complete package identity: one marker byte after each of the first 20 blocks"""
    assert len(raw) >= BLOCKS * BLK, "package is too short"
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("sounds", type=Path)
    parser.add_argument("version")
    parser.add_argument("output", type=Path, nargs="?", default=HERE.parent / "public/fm1/install")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--source-archive", type=Path)
    args = parser.parse_args(argv)
    pkg, snd, version, out = args.package, args.sounds, args.version, args.output
    assert re.fullmatch(r"[0-9A-Za-z.-]+", version), version
    raw, sounds = pkg.read_bytes(), snd.read_bytes()
    product = product_of(raw)
    manifest = json.loads(args.manifest.read_text()) if args.manifest else None
    expected_product = manifest.get("product", "FM-1_900") if manifest else "FM-1_900"
    assert product == expected_product, f"package identity {product!r}; expected {expected_product!r}"
    assert len(sounds) == SOUND_BYTES and sounds[:4] == b"BBSK", "not a BB1 sound-library image"
    if product == "FM-1_92200201":
        source = HERE.parents[1]
        sys.path.insert(0, str(source / "tools"))
        from bb_library_identity import verify_library
        verify_library(snd, source)
    pkg_name, snd_name, source_name = f"bb1-{version}.fwsc", f"bb1-sounds-{version}.bin", f"bb1-source-{version}.tar"
    if manifest:
        assert manifest["version"] == version, "manifest display version differs"
        package_file = next(f for f in manifest["files"] if f["name"].endswith(".fwsc"))
        sound_file = next(f for f in manifest["files"] if f["name"].endswith(".bin"))
        source_file = next(f for f in manifest["files"] if f["name"].startswith("bb1-source-"))
        pkg_name, snd_name, source_name = package_file["name"], sound_file["name"], source_file["name"]
        for name in (pkg_name, snd_name, source_name):
            assert Path(name).name == name and re.fullmatch(r"[0-9A-Za-z.-]+", name), "non-local release filename"
        for file, data in ((package_file, raw), (sound_file, sounds)):
            assert file["bytes"] == len(data) and file["sha256"] == hashlib.sha256(data).hexdigest(), "manifest artifact pin differs"
    page = (HERE / "index.tpl.html").read_text()
    assert page.count("/*META*/") == 1 and page.count("__SOURCE_FILE__") == 1
    source_data = args.source_archive.read_bytes() if args.source_archive else None
    if product == "FM-1_92200201":
        assert manifest and source_data, "2.1 requires its source archive"
    if source_data is not None:
        assert manifest, "source archive requires a manifest pin"
        assert source_file["bytes"] == len(source_data), "source archive size differs"
        assert source_file["sha256"] == hashlib.sha256(source_data).hexdigest(), "source archive hash differs"
    meta = {"version": version, "product": product,
            "pkg": f"files/{pkg_name}", "pkgBytes": len(raw), "pkgSha256": hashlib.sha256(raw).hexdigest(),
            "sounds": f"files/{snd_name}", "soundsSha256": hashlib.sha256(sounds).hexdigest()}
    if product in ("FM-1_92200200", "FM-1_92200201"):
        assert manifest and "startingProducts" in manifest, "2.x requires an explicit starting-identity policy"
    if manifest and "startingProducts" in manifest:
        starts = manifest["startingProducts"]
        assert isinstance(starts, list) and starts and len(set(starts)) == len(starts), "invalid starting identities"
        assert all(isinstance(x, str) and re.fullmatch(r"FM-1_[0-9]+", x) for x in starts), "invalid starting identity"
        meta["startingProducts"] = starts
    if manifest and "displayName" in manifest:
        meta["displayName"] = manifest["displayName"]
    files = out / "files"
    artifacts = [(pkg_name, raw), (snd_name, sounds)]
    if source_data is not None:
        artifacts.append((source_name, source_data))
    # Other versions stay available for a download already in progress.
    for name, data in artifacts:
        existing = files / name
        assert not existing.exists() or existing.read_bytes() == data, f"refusing to overwrite frozen release: {name}"
    out.mkdir(parents=True, exist_ok=True)
    files.mkdir(exist_ok=True)
    for name, data in artifacts:
        if not (files / name).exists():
            (files / name).write_bytes(data)
    for m in ("fm1ota.js", "fm1pkg.js", "fm1backup.js", "bb1sounds.js", "backup.html", "compatibility.html"):
        shutil.copyfile(HERE / m, out / m)
    (out / "index.html").write_text(page.replace("/*META*/", json.dumps(meta)).replace("__SOURCE_FILE__", source_name))
    print(json.dumps(meta, indent=2))


if __name__ == "__main__":
    main(sys.argv[1:])
