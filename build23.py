#!/usr/bin/env python3 -I
"""Build BB1 2.3 using the exact same pipeline as build_from_source.py."""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path

SRC = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--pins", type=Path, default=SRC / "RELEASE-BUILD-PINS.json",
                    help="Explicit expected build identity and hash; mismatch always fails")
args = parser.parse_args()
pins = json.loads(args.pins.read_text())
TC = Path(os.environ["JIELI_TOOLCHAIN"]).resolve()
OUT = SRC / "build"
CMDS = SRC / "big-beat" / "integration" / "build" / "cmds-r4-onepass"
TOOLS = SRC / "tools"
recipe = json.loads((SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21" / "recipe.json").read_text())
# BB1 2.3: override recipe flags for code space
candidate_defines = dict(pins["candidate_defines"])
candidate_defines["FELUCCA_ID"] = json.dumps(pins["product"])
if not re.fullmatch(r"FM-1_[0-9]{1,10}", pins["product"]):
    raise SystemExit("Invalid pinned product identity")
if not re.fullmatch(r"[0-9a-f]{64}", pins["expected_app_sha256"]):
    raise SystemExit("A pinned application SHA256 is required")


def rewrite(args, unit=None):
    """Exact replica of build_from_source.py rewrite()."""
    out = []
    for a in args:
        a = a.replace("/work/source", str(SRC)).replace("/work/group_target_build-r1", str(OUT))
        a = a.replace("/opt/jieli/", str(TC) + "/")
        a = a.replace("-I/assets", f"-I{SRC}/assets" if (SRC/"assets").is_dir() else f"-I{SRC}")
        a = a.replace("-I/generated", f"-I{OUT}/gen")
        out.append(a)
    for old, new in recipe.get("flag_edits", {}).items():
        out = [new if a == old else a for a in out if not (new is None and a == old)]
    out += recipe.get("extra_flags", [])
    for name, value in sorted(candidate_defines.items()):
        out = [a for a in out if not (a.startswith("-D") and a[2:].split("=", 1)[0] == name)]
        out.append(f"-D{name}={value}")
    for old, new in recipe.get("unit_flag_edits", {}).get(unit or "", {}).items():
        if old in out:
            out[out.index(old)] = new
    return out

def run_tc(tool, args, label):
    # Remove -MMD and -MF pairs
    cleaned = []
    skip = False
    for a in args:
        if skip: skip = False; continue
        if a == "-MMD": continue
        if a == "-MF": skip = True; continue
        cleaned.append(a)
    p = subprocess.run([str(TC / tool), *map(str, cleaned)], cwd=SRC, capture_output=True, text=True, timeout=300)
    if p.returncode:
        errors = [l for l in p.stderr.splitlines() if ": error:" in l]
        print(f"  FAILED {label}: {len(errors)} errors")
        for e in errors[:5]: print(f"    {e}")
        if not errors and p.stderr.strip():
            print(f"  stderr: {p.stderr.strip()[:500]}")
        return False
    return True

OUT.mkdir(exist_ok=True)

# 1. Compile all BB units (not main)
order = recipe["link_order"]
# BB1 2.3: replace bb_echo + bb_spring with bb_fxbus
order = [n for n in order if n not in ("bb_echo", "bb_spring")]
order.insert(order.index("bb_output"), "bb_fxbus")
# BB1 2.3: add bb_dillo_kick and bb_learn_report for Dillo kick response and Sideman TX
for new_unit in ("bb_dillo_kick", "bb_learn_report", "bb_screen_cast",
                 "bb_editor", "bb_editor_snapshot"):
    if new_unit not in order:
        order.insert(order.index("bb_wire") + 1, new_unit)
# BB1 2.3: add bb_profile for per-stage audio profiling
if "bb_profile" not in order:
    order.insert(order.index("bb_audio") + 1, "bb_profile")
# Units to recompile at -Oz for code space
# bb_x0x_drum808 and bb_x0x_bass303 cannot use -Oz: .ram_text relocations.
# bb_audio, bb_core, crt0, fm1_isr, fm1_vec: timing-critical, keep -O2.
# 2.3: apply -Oz aggressively to free space for UI wiring + embedded help.
OZ_UNITS = {"bb_fxbus", "bb_factory", "bb_project", "bb_arrangement",
            "bb_arr_sources", "bb_arr_controller", "bb_gen", "bb_browser",
            "bb_player", "bb_audio_section", "bb_audio_phrase",
            "bb_audio_phrase_render", "bb_paged_records", "bb_factory_drum",
            "bb_phrase_nudge", "bb_control", "bb_audio_pattern",
            "bb_catalogue", "bb_evolve", "bb_home_control", "bb_home_service",
            "bb_panel", "bb_session", "bb_live_record", "bb_live_record_arr",
            "bb_performer", "bb_sound_picker", "bb_user_kit", "bb_wire",
            "bb_output", "bb_dillo", "bb_editor", "bb_editor_snapshot", "bb_factory_launch",
            "bb_factory_drum_launch", "bb_factory_pattern", "bb_factory_recipe",
            "bb_catalogue_page", "bb_paged_container", "bb_delayed",
            "bb_phrase", "bb_phrase_controls", "bb_phrase_index",
            "bb_phrase_shaker", "bb_phrase_strike", "bb_rich_scene",
            "bb_family_scene", "bb_expression_scene", "bb_note_expression",
            "bb_note_expression_catalogue", "bb_crc32",
            "group_gain", "group_scene", "sample_scene",
            "bb909_controls", "bb909_patch", "bb909_sequence",
            "drum909", "transfer"}

print(f"Building {len(order)} units...")
failed = []
for name in order:
    if name == "main": continue
    # New 2.3 units: derive compile commands from an existing BB unit
    if name in ("bb_fxbus", "bb_dillo_kick", "bb_learn_report", "bb_screen_cast",
                "bb_editor", "bb_editor_snapshot"):
        base = "bb_echo"  # any BB unit's command works as a template
        cmd = json.loads((CMDS / f"{base}-o-command.json").read_text())
        cmd = [a.replace(base, name) for a in cmd]
    else:
        cmd_file = CMDS / f"{name}-o-command.json"
        if not cmd_file.exists():
            print(f"  SKIP {name} (no command file)")
            failed.append(name)
            continue
        cmd = json.loads(cmd_file.read_text())
    assert cmd[0] == "-target", name
    args = rewrite(cmd, name)
    # Apply -Oz to designated units
    if name in OZ_UNITS:
        args = ["-Oz" if a == "-O2" else a for a in args]
    if not run_tc("pi32v2/bin/clang", args, f"{name}-o"):
        failed.append(name)
    else:
        tag = " [Oz]" if name in OZ_UNITS else ""
        sys.stdout.write(f"  {name}{tag} "); sys.stdout.flush()

print(f"\n{len(order) - 1 - len(failed)} BB units OK, {len(failed)} failed")
if failed:
    raise SystemExit(f"Failed compilation units: {failed}")

# 2. Main: unoptimised IR -> size attributes -> codegen
print("\nBuilding main unit (felucca.c)...")
front = rewrite(json.loads((CMDS / "main-front-command.json").read_text()))
if not run_tc("pi32v2/bin/clang", front, "main-front"):
    sys.exit("main IR generation failed")
print("  IR generated")

raw, sized = OUT / "main.ll", OUT / "main-size.ll"
# Run size_fns.py
ns = {"__file__": str(SRC / "tools/size_fns.py"), "__name__": "size_fns"}
exec(compile((TOOLS / "size_fns.py").read_text(), "size_fns", "exec"), ns)
ns["main"](raw, sized)
print("  Size pass done")

# Run mark_ui.py (additional UI function marking, matching build_from_source.py)
BTOOLS = SRC / "big-beat" / "integration" / "build" / "tools"
marker_src = (BTOOLS / "mark_ui.py").read_text()
marker_src = marker_src.replace("Path('/work/target/tools/size_fns.py')", f"Path('{BTOOLS}/size_fns.py')")
marker_src = marker_src.replace("/work/target/", f"{SRC}/")
(OUT / "mark_ui.py").write_text(marker_src)
m_result = subprocess.run([sys.executable, str(OUT / "mark_ui.py"), str(sized)],
                         capture_output=True, text=True, timeout=60)
if m_result.returncode:
    raise SystemExit(f"mark_ui.py failed: {m_result.stderr[:200]}")
else:
    print(f"  mark_ui.py: {m_result.stdout.strip()}")

# Apply size_include (mark main-loop UI functions as minsize)
inc = recipe.get("size_include")
if inc:
    pre = [a for a in front if a not in ("-S", "-emit-llvm", "-MMD")]
    pre = [a for i, a in enumerate(pre) if not (a == "-MF" or (i and pre[i - 1] == "-MF"))]
    idx = pre.index("-o"); pre[idx + 1] = str(OUT / "main.i"); pre.insert(idx, "-E")
    if not run_tc("pi32v2/bin/clang", pre, "main-pre"):
        sys.exit("preprocessor failed")
    import bisect
    text = (OUT / "main.i").read_text(errors="replace")
    defined = set(m.group(2) for m in ns["IR_DEF"].finditer(raw.read_text()))
    marks = [(m.start(), m.group(1).split("firmware/src/")[-1]) for m in re.finditer(r'^# \d+ "([^"]+)"', text, re.M)]
    at = [p for p, _ in marks]
    files, where = set(inc["files"]), {}
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        name = m.group(1)
        if name not in defined or name in where: continue
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0); i += 1
            if depth == 0: break
        if re.match(r"\s*(__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)\s*)*\{", text[i:i + 300]):
            where[name] = marks[bisect.bisect_right(at, m.start()) - 1][1]
    wanted = set(n for n, f in where.items() if f in files)
    ns["ISR_ROOTS"] = tuple(ns["ISR_ROOTS"]) + tuple(inc["timing_roots"])
    timing = ns["isr_reach"](raw.read_text())
    ir, hit = sized.read_text(), set()
    def add_minsize(m):
        if m.group(2) not in wanted or m.group(2) in timing or " minsize" in m.group(1):
            return m.group(0)
        hit.add(m.group(2)); return m.group(1) + " minsize" + m.group(3)
    ir = ns["IR_DEF"].sub(add_minsize, ir)
    sized.write_text(ir)
    kept = sorted(wanted & timing)
    excl = recipe.get("size_exclude", [])
    if excl:
        n = 0
        for name in excl:
            ir2, k = re.subn(r"^(define [^\n]*@" + re.escape(name) + r"\([^\n]*\)) minsize ", r"\1 ", ir, flags=re.M); n += k; ir = ir2
        sized.write_text(ir)
    print(f"  size_include: {len(hit)} functions marked minsize ({len(kept)} timing-kept)")

# Compile sized IR to object
main_o_cmd = rewrite(json.loads((CMDS / "main-o-command.json").read_text()))
if not run_tc("pi32v2/bin/clang", main_o_cmd, "main-o"):
    sys.exit("main codegen failed")
# The output is main.o but we need it as felucca.o for our link script
import shutil
if (OUT / "main.o").exists():
    shutil.copy2(OUT / "main.o", OUT / "felucca.o")
print("  Main unit compiled")

# 3. Link
print("\nLinking...")
RECIPE_DIR = SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21"
# Container paths → local paths
ld_script = recipe["linker_script"]
if ld_script.startswith("/link/"):
    ld_script = str(RECIPE_DIR / "link" / ld_script[len("/link/"):])
asset_symbols = recipe.get("asset_symbols", "")
if asset_symbols.startswith("/assets/"):
    # The assets dir is mounted from a specific path — find it locally
    local_assets = SRC / "big-beat" / "integration" / "build" / "recipes" / "composition-21" / "assets"
    if not local_assets.exists():
        asset_symbols = str(SRC / "asset-symbols.ld")
    else:
        asset_symbols = str(local_assets / asset_symbols[len("/assets/"):])
flags = ["--gc-sections", "-Map=" + str(OUT / "app.map"), "-T", ld_script]
if asset_symbols and Path(asset_symbols).exists():
    flags += ["-T", asset_symbols]
for root in recipe.get("roots", []):
    flags += ["-u", root]
objs = [OUT / f"{n}.o" for n in order]
if not run_tc("pi32v2/bin/ld", flags + [str(o) for o in objs] + ["-o", str(OUT / "app.elf")], "link"):
    sys.exit("link failed")

# Build proper binary like build_from_source.py does (each section separately)
symbols = subprocess.run([str(TC / "common/bin/objdump"), "-t", str(OUT / "app.elf")],
                        capture_output=True, text=True, check=True).stdout
def addr(n):
    m = re.search(r"^([0-9a-f]+).*\s" + re.escape(n) + r"$", symbols, re.M)
    return int(m[1], 16)

blob = bytearray()
for section, sym in [("text", "_start"), ("ram_text", "_rt_load"), ("data", "_data_load")]:
    path = OUT / f"{section}.bin"
    subprocess.run([str(TC / "common/bin/objcopy"), "-O", "binary", "-j", "." + section,
                   str(OUT / "app.elf"), str(path)], check=True)
    at = addr(sym) - 0x02000120
    assert at >= len(blob), (section, at, len(blob))
    blob += b"\xff" * (at - len(blob)) + path.read_bytes()
blob += b"\xff" * (-len(blob) % 4)
(OUT / "app.bin").write_bytes(blob)

size = len(blob)
code_free = 581564 - size
ram_free = 0x1c1a000 - addr("_bss_end")
pool_free = 0x1c74000 - addr("_pool_end")
print(f"\n{'='*50}")
print(f"app.bin: {size:,} bytes")
print(f"Code free: {code_free:,} bytes")
print(f"RAM free: {ram_free:,} bytes")
print(f"Pool free: {pool_free:,} bytes")
print(f"R174 baseline: 581,160 bytes (404 free)")
print(f"Delta from R174: {size - 581160:+,} bytes")
fits = code_free >= 0 and ram_free >= 4096 and pool_free >= 8192
print(f"Resource limits: {'PASS' if fits else 'FAIL'}")

if not fits:
    raise SystemExit("Application exceeds resource limits")
expected = pins["expected_app_sha256"]
actual = hashlib.sha256((OUT / "app.bin").read_bytes()).hexdigest()
print(f"Application SHA256: {actual}")
if actual != expected:
    raise SystemExit(f"Application hash differs from release pin: {actual}")
