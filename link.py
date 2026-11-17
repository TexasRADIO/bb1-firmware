#!/usr/bin/env python3 -I
import json, subprocess, os
from pathlib import Path

OUT = Path("build")
TC = Path(os.environ["JIELI_TOOLCHAIN"])
recipe = json.loads(Path("big-beat/integration/build/recipes/composition-21/recipe.json").read_text())
ld_script = Path("big-beat/integration/build/recipes/composition-21/link/run-r3/guarded-gc.ld")

objects = []
for name in recipe["link_order"]:
    obj = OUT / f"{name}.o"
    if name == "main":
        obj = OUT / "felucca.o"
    if obj.exists():
        objects.append(str(obj))
    else:
        print(f"MISSING: {name}")

print(f"{len(objects)} objects in link order")
elf = OUT / "felucca.elf"
cmd = [str(TC / "pi32v2/bin/ld"), "--gc-sections", "-e", "_start",
       "-T", str(ld_script)] + objects + ["-o", str(elf)]

r = subprocess.run(cmd, capture_output=True, text=True)
if r.returncode:
    seen = set()
    for line in r.stderr.splitlines():
        key = line.strip()
        if key not in seen:
            seen.add(key)
            print(line)
        if len(seen) > 20:
            break
    print(f"\nLink exit: {r.returncode}")
else:
    subprocess.run([str(TC / "common/bin/objcopy"), "-O", "binary",
                   "-j", ".text", "-j", ".data", str(elf), str(OUT / "felucca.bin")], check=True)
    size = (OUT / "felucca.bin").stat().st_size
    print(f"\nfelucca.bin: {size:,} bytes")
    print(f"R174 baseline (BB1 2.2): 581,160 bytes (404 bytes free)")
    delta = size - 581160
    print(f"Delta: {delta:+,} bytes")
    headroom = 581160 + 404 - size
    print(f"Headroom: {headroom:,} bytes free")
