# MIDI input at project release

The project owner holds audio and kit/session processing while it snapshots, validates or restores a file. Input received during this hold must not replay after release. The ordinary MIDI paused handler can be bypassed by the earlier asset-hold return, so project release now flushes the shared input ring and compatibility state while the main owner excludes IRQ producers. It requests source-specific note release and clears only the old discard state, preserving packets arriving after release. It does not touch the paused sound actor or command queue directly.

Seven real full-unity owner paths reproduce stale replay before the fix and pass afterward: import, cancel, arrival after staging, export, invalid import, timeout, and cancellation with a previously held note. A full 64-slot input ring also clears at release: the first new note is accepted. Diagnostic injection uses the real MIDI ring with TRS source tagging; it is not physical MIDI or IRQ concurrency proof.

Normal emulator regression: 69 exports unchanged; 342 actual C framebuffers, 481536 stereo values and the 2496-byte project roundtrip match the previous private build. The paired sampled/native tests compare another 794112 stereo values and verify eight project imports with clock ON/OFF, retaining the earlier output-endpoint fix.

Fresh native qualification: 66 objects; only main.o changes, 65 providers identical. +64 code bytes, application 580352 bytes, SHA-256 47ec7d857d92e3894ec62ae3ab28490ee34a948fdcc98d64f16ae307d983b5d5; 1212 code bytes free, ordinary RAM free9284/pool free30852/RAMtext24028 unchanged. Empty linker log. The pre-commit isolated source manifest matches every one of the 770 native source inputs after applying this change.

Evidence and runnable review fixtures: /Volumes/Orange Base/Codex Working Cache/tmp/bb1-20-integration-20261030/wasm-midi-input-hold-20261031; PR68 verification-source/midi-project-hold-20261031. The full target build is qualified by source-content identity, not by the earlier baseline commit name in its isolated reports.

Private development only. No package, device write or public release. Complete stack, native execution/timing, physical controls/listening and recovery acceptance remain open; earlier PC-specific stack maps do not qualify this changed image.
