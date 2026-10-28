# BB1 for FM-1

BB1 turns the M-VAVE FM-1 into two drum machines and a bass synth, with tape echo and a spring reverb.

**Status: staging. Not released.** This repository holds the complete corresponding source for BB1, under the GNU GPL v3. The V1 release, its notes and the downloads land here when V1 is accepted.

- **Install:** the web installer at thebigbeat.app/fm1/install/ (live at release).
- **Guide:** thebigbeat.app/fm1/guide/.

## Build and verify

Run `big-beat/integration/verify_release.py` with this tree and a new output directory. It runs the host tests, the audio oracle, the level sweep, the from-source target build and the deterministic packaging. With `--expect-app` / `--expect-package`, it confirms the release identities. The pinned external build inputs are listed in `big-beat/integration/inputs.json`.

Licensing: `LICENSE`, `LICENSING.md`, `LICENSES/` and `BIG-BEAT-LICENSING.md`.
