# Big Beat Studio BB1 2.1 native profile

Joe selected the no-SLOOP direction on 3 November 2026. This profile derives from composition-20-lean and explicitly requires both SLOOP flags to be zero. It retains the two drum layers, Dillo, echo/spring, USER kit builder and existing composition/recording services. It does not remove source, licence notices, library bytes or saved scenes.

The comparison at source 026f60fc51142d63878f429877209d556e4c60fd produced app 5a67102dfd309f67ecaa0c8d76c272909f35d3208e02f6798cbadad65b636b48, 573088 bytes, 8476 code bytes free. This is historical measurement using composition-20-lean, not a build receipt for this explicit recipe or later source.

SLOOP-dependent BBKS scenes are unsupported in this build. Joe explicitly accepts that older SLOOP-dependent saves may no longer open; migration, backward compatibility and special refusal messaging for those saves are not release requirements. Continue development without that workstream. Normal correctness of current save/load functionality remains part of testing.

Use cmds-r4-onepass with run_build.sh and this recipe. Final source/app/library pins, memory and stack checks, runtime timing, human acceptance, synchronized installer/emulator/manual and publication approval remain open.

## Release verification

From the source checkout, use the selected profile explicitly:

```sh
python3 big-beat/integration/verify_release.py "$PWD" /path/to/new-verification-output --recipe composition-21 --sound-library /path/to/bb1-sounds-2.1.bin
```

The library argument is mandatory for this profile, including host-only verification. The level sweep uses this same verified library. Its full SHA-256 and help-block pins must match `big-beat/sound-library.json` and the compiled source help definitions. A firmware-only FWSC does not contain this library; the receipt names the separately verified library it requires.

`--host-only` omits native compilation and packaging, but still runs pinned input checks, inventory, host tests, Conductor/HOLD/welcome proofs, worker/serial comparisons and the level sweep. It does not prove device timing or human acceptance. The Conductor and HOLD fixtures use the selected profile's USER/SLOOP switches. The 2.1 navigation matrix must match the reviewed baseline; changes require review before updating it.
