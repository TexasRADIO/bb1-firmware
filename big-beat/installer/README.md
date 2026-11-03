# BB1 2.1 installer integration

Imported from website commit `0372632d00c8dd3c5b4b89c200482724bac7f0ca`; `IMPORT.json` records exact original file blobs. Upstream GPL notices and Felucca attribution are retained.

The template now includes the reviewed Install / Remove / Help navigation and official V15 return explanation. Its product text describes 2.1, the matching library, retained USER builder and removed SLOOP support. Old SLOOP project migration is not required.

The generator validates package/library bytes and explicit starting identities before writing output. It preserves existing frozen artifacts. Both 2.0 and 2.1 packages require a starting-product policy; 2.1 review uses corrected 1.5, public 2.0 and 2.1 identities. This is an installer policy, not proof that all transition paths have been exercised on hardware.

The current 2.1 installer must pin one reviewed FWSC, the matching library from `big-beat/sound-library.json`, and an exact archive of the same source commit. Missing or duplicate starting policy, wrong package hash or product, altered library, and missing or altered source all refuse before output creation.

This is not a published or device-qualified installer. The source archive, help/backup routes, current website integration and browser/device qualification must be supplied and verified before release. Official V15 is deliberately user-supplied from M-VAVE, not bundled: its unchanged file is validated by exact size and SHA-256 in fm1pkg.js. The staged source-archive link is not yet populated.

Repeatable offline check: `python3 big-beat/installer/test_generation.py PACKAGE.fwsc SOUNDS.bin SOURCE.tar`. It verifies valid generation, missing/duplicate identity policy, wrong package hash/product, and an altered library even when its manifest agrees with the alteration. It also verifies repeat generation and that a conflicting frozen artifact leaves all existing output bytes unchanged. The 2.1 generator checks the library against the firmware source help block and library metadata. The current real-artifact run passes all eight cases plus repeat/frozen-output checks.

2.1 generation now requires `--source-archive` and its manifest size/hash. The source download is copied and protected against overwrite alongside the firmware and sound library. Eight cases pass, including missing/mismatched source; the source check proves archive identity, while firmware correspondence is separately checked against the native-build source.
