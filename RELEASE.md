# BB1 2.3.1

BB1 combines two drum layers, sampled kits, a synthesised 808 and Dillo bass,
with effects, browser editing, per-song effects recall, retrospective capture,
Screen companion and Sideman USB transmission. This package targets the M-VAVE
FM-1 and identifies as `FM-1_92200231`.

The installer resumes a sound-library transfer from the cursor accepted by the
firmware, verifies the complete bank before restart, confirms the returned
product identity and checks the running application's seven-kit admission
receipt. A runtime admission report identifies loaded kit data; it does not
establish audible playback. An incomplete or unsupported report remains
unverified and can be downloaded for troubleshooting.

For sound-library transfer, stop playback and turn MASTER fully down. Keep USB
connected until the installer finishes. The shared editor and matching
`bb1-sounds-2.3.bin` library are retained.

[Install BB1](https://thebigbeat.app/fm1/install/) · [Browser editor](https://thebigbeat.app/editor/) · [Player’s guide](https://thebigbeat.app/fm1/guide/) · [Browse source](https://github.com/TexasRADIO/bb1-firmware/tree/v2.3.1)

## Downloads and source

- `bb1-2.3.1.fwsc`: application and matching RAM update loader.
- `bb1-sounds-2.3.bin`: matching sampled-kit library.
- `bb1-source-2.3.1.tar.gz`: corresponding source, build instructions and licence notices.
- `RELEASE.json` and `SHA256SUMS`: download identities and checksums.

Use `RELEASE-BUILD-PINS.json` for the active application build. `BUILD-INPUTS.json`
records the unchanged source, compiler commands, recipes and generated inputs
for the pinned application, loader and package. Firmware is GPL-3.0-only;
component and sound-library terms remain in `LICENSING.md`,
`BIG-BEAT-LICENSING.md` and `LICENSES/`.
