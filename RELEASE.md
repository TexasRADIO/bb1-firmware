# BB1 2.3 native Hub package

This distribution combines the unchanged BB1 2.3 shared-editor application and
its matching sampled-kit library into one `.fwsc` for the M-VAVE FM-1.
Firmware version: 2.3. Product: `FM-1_92200203`. Packaging tag: `v2.3-hub.1`.

The loader checks the complete package before erasing, writes only its declared
application/content windows, verifies each written sector and skips identical
sectors on a resumed installation. Boot data, calibration and unrelated user
storage remain outside those windows. Keep USB and power connected throughout
installation, and back up music before switching firmware families.

## Files

- `bb1-2.3.fwsc`: the complete application and matching sound library.
- `bb1-source-2.3-hub.1.tar.gz`: corresponding source and build instructions.
- `RELEASE.json` and `SHA256SUMS`: package identity and checksums.
- `fmhub.json` and `bb1-hub-card.jpg`: product card and image.
- Licence files: firmware, component and sound-library terms.

[Player’s guide](https://thebigbeat.app/fm1/guide/) ·
[Browser editor](https://thebigbeat.app/editor/) ·
[Corresponding source](https://github.com/TexasRADIO/bb1-firmware/tree/v2.3-hub.1)
