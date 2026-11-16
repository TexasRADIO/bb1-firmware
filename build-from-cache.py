#!/usr/bin/env python3 -I
"""Build BB1 2.3 using the cached compilation commands from the verified 2.2 build.
Replaces /work/source/ paths with the local tree and /opt/jieli/ with the local toolchain."""
import json, os, subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

SRC = Path(__file__).resolve().parent
TC = Path(os.environ["JIELI_TOOLCHAIN"]).resolve()
SDK = Path(os.environ["AC79_SDK"]).resolve()
OUT = SRC / "build"
CMDS = SRC / "big-beat" / "integration" / "build" / "cmds-r4-onepass"
RECIPE = json.loads((SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21" / "recipe.json").read_text())

def rewrite(args):
    """Rewrite container paths to local paths."""
    cleaned = []
    skip_next = False
    for a in args:
        if skip_next:
            skip_next = False
            continue
        if a == "-MMD":
            continue
        if a == "-MF":
            skip_next = True
            continue
        a = a.replace("/work/source/", str(SRC) + "/")
        a = a.replace("/work/group_target_build-r1/", str(OUT) + "/")
        a = a.replace("/opt/jieli/", str(TC) + "/")
        if a == "-I/assets":
            a = f"-I{SRC}/assets" if (SRC/"assets").is_dir() else f"-I{SRC}"
        elif a == "-I/generated":
            a = f"-I{OUT}/gen"
        cleaned.append(a)
    return cleaned

def tc(args):
    """Run a toolchain command."""
    # First arg after rewrite might be 'pi32v2/bin/clang' etc
    if args[0] == "-target":
        # This is a clang command, prepend the compiler
        args = [str(TC / "pi32v2" / "bin" / "clang")] + args
    r = subprocess.run(args, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        errors = [l for l in r.stderr.splitlines() if ": error:" in l]
        print(f"FAILED: {Path(args[-1]).name}")
        for e in errors[:5]:
            print(f"  {e}")
        return False
    return True

OUT.mkdir(exist_ok=True)

# 1. Generate assets (fonts, icons, etc.) — already done if build/gen exists
if not (OUT / "gen" / "ui_fonts.h").exists():
    print("Generating assets...")
    subprocess.run([sys.executable, "-I", str(SRC / "tools" / "build.py")], cwd=SRC, env={
        **os.environ, "FELUCCA_SLICE": "0"
    })

# 2. Build the loader (reuse from build.py)
print("Building loader...")
subprocess.run([sys.executable, "-I", str(SRC / "tools" / "build.py")], cwd=SRC, env={
    **os.environ, "FELUCCA_SLICE": "0"
})  # This will fail at the app but the loader should be built

# 3. Build the main unit using cached front command
print("\n=== Building main unit (felucca.c) ===")
front_cmd = json.loads((CMDS / "main-front-command.json").read_text())
# Add recipe flags only if not already defined (avoid macro redefinition)
existing_defs = {f.split("=")[0] for f in front_cmd if f.startswith("-D")}
for f in RECIPE.get("extra_flags", []):
    if f.startswith("-D") and f.split("=")[0] not in existing_defs:
        front_cmd.append(f)
for k, v in RECIPE.get("required_profile_flags", {}).items():
    if f"-D{k}" not in existing_defs:
        front_cmd.append(f"-D{k}={v}")
for k, v in RECIPE.get("required_disabled_flags", {}).items():
    if f"-D{k}" not in existing_defs:
        front_cmd.append(f"-D{k}={v}")

front_args = rewrite(front_cmd)
# The front command compiles to LLVM IR
front_args = [str(TC / "pi32v2" / "bin" / "clang")] + front_args
# Replace output path
for i, a in enumerate(front_args):
    if a.endswith("main-size.ll"):
        front_args[i] = str(OUT / "felucca.ll")
    if a.endswith("felucca.c") and not Path(a).exists():
        front_args[i] = str(SRC / "firmware" / "src" / "felucca.c")

# Actually the front command goes felucca.c -> felucca.ll, then size pass, then compile
# Let's compile directly to .o instead of going through LLVM IR
front_direct = [a for a in front_args if a not in ("-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns")]
for i, a in enumerate(front_direct):
    if a.endswith("felucca.ll"):
        front_direct[i] = str(OUT / "felucca.o")
    if a == "-o" and i+1 < len(front_direct) and front_direct[i+1].endswith("felucca.ll"):
        front_direct[i+1] = str(OUT / "felucca.o")

if not tc(front_direct):
    sys.exit("Main unit failed")
print("Main unit: OK")

# 4. Build BB objects from cached commands
print("\n=== Building BB objects ===")
recipe_units = RECIPE["link_order"]
failed = []
built = []

def build_unit(name):
    cmd_file = CMDS / f"{name}-o-command.json"
    if not cmd_file.exists():
        return (name, "no command file")
    cmds = json.loads(cmd_file.read_text())
    # The cached commands already have the exact flags from the verified build.
    # Do NOT add recipe flags — they would enable extra features and grow the binary.
    args = rewrite(cmds)
    args = [str(TC / "pi32v2" / "bin" / "clang")] + args
    r = subprocess.run(args, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        errors = [l for l in r.stderr.splitlines() if ": error:" in l]
        return (name, errors[:3])
    return (name, None)

# Also build startup units
startup = ["crt0", "fm1_vec", "fm1_isr", "bb_core1_probe"]
all_units = startup + recipe_units

with ThreadPoolExecutor(8) as ex:
    results = list(ex.map(build_unit, all_units))

for name, err in results:
    if err:
        failed.append((name, err))
    else:
        built.append(name)

print(f"\nBuilt: {len(built)} units")
if failed:
    print(f"Failed: {len(failed)} units")
    for name, err in failed:
        print(f"  {name}: {err}")

# 5. Link
if not failed:
    print("\n=== Linking ===")
    objects = []
    for name in ["crt0", "fm1_vec", "fm1_isr", "main"] + recipe_units + ["bb_core1_probe"]:
        obj = OUT / f"{name}.o"
        if not obj.exists() and name == "main":
            obj = OUT / "felucca.o"
        if obj.exists():
            objects.append(str(obj))
        else:
            print(f"  Missing: {name}.o")

    ld_script = SRC / "firmware" / "app.ld"
    # Check for recipe linker script
    recipe_ld = RECIPE.get("linker_script", "")
    if recipe_ld:
        candidate = SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21" / recipe_ld.lstrip("/")
        if candidate.exists():
            ld_script = candidate

    asset_syms = RECIPE.get("asset_symbols", "")
    asset_ld = ""
    if asset_syms:
        candidate = SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21" / asset_syms.lstrip("/")
        if candidate.exists():
            asset_ld = str(candidate)

    elf = OUT / "felucca.elf"
    ld_cmd = [str(TC / "pi32v2" / "bin" / "ld"), "--gc-sections", "-e", "_start",
              "-T", str(ld_script)]
    if asset_ld:
        ld_cmd += ["-T", asset_ld]
    ld_cmd += objects + ["-o", str(elf)]

    r = subprocess.run(ld_cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        print(f"Link failed:\n{r.stderr[:1000]}")
    else:
        # Extract binary
        subprocess.run([str(TC / "common" / "bin" / "objcopy"), "-O", "binary",
                       "-j", ".text", "-j", ".data", str(elf), str(OUT / "felucca.bin")],
                      cwd=SRC, check=True)
        app_size = (OUT / "felucca.bin").stat().st_size
        print(f"\n✓ felucca.bin: {app_size} bytes")
        print(f"  Code headroom: {581160 - app_size} bytes vs R174 baseline")
