from pathlib import Path
import subprocess,json,shlex,re,os,sys,hashlib
sys.dont_write_bytecode=True
import argparse
ap=argparse.ArgumentParser(description='Build the BB1 browser module from the selected 2.1 source and included adapter.')
ap.add_argument('source',type=Path)
ap.add_argument('output',type=Path)
ap.add_argument('--recipe',default='composition-21',help='integration recipe name (default: composition-21)')
ap.add_argument('--define',action='append',default=[],metavar='BB_NAME=VALUE',
                help='explicit candidate-only BB_ feature override; repeatable')
a=ap.parse_args()
R=Path(__file__).resolve().parent;S=a.source.resolve();P=R;H='emu-release.c';B=a.output.resolve()
assert not B.exists(), 'Refuse output reuse'
B.mkdir(parents=True);(B/'obj').mkdir()

assert re.fullmatch(r'[A-Za-z0-9._-]+',a.recipe), 'Invalid recipe name'
recipe=S/'big-beat/integration/build/recipes'/a.recipe/'recipe.json'
assert recipe.is_file(), f'Missing recipe: {recipe}'
native=json.loads(recipe.read_text())
# Keep the browser qualification build on the same 2.3 unit graph as the
# native candidate.  The pinned composition recipe predates the consolidated
# FX bus and the editor/companion units; using it verbatim leaves those symbols
# as WebAssembly imports and makes every behavior test fail before boot.
link_order=[n for n in native['link_order'] if n not in ('bb_echo','bb_spring')]
link_order.insert(link_order.index('bb_output'),'bb_fxbus')
for unit in ('bb_dillo_kick','bb_learn_report','bb_screen_cast',
             'bb_editor','bb_editor_snapshot'):
 if unit not in link_order:link_order.insert(link_order.index('bb_wire')+1,unit)
if 'bb_profile' not in link_order:link_order.insert(link_order.index('bb_audio')+1,'bb_profile')
native['link_order']=link_order
derived_units={'bb_fxbus','bb_dillo_kick','bb_learn_report','bb_screen_cast',
               'bb_editor','bb_editor_snapshot','bb_profile'}
identity=json.loads((S/'big-beat/identity.json').read_text())
overrides={}
for value in a.define:
 assert re.fullmatch(r'BB_[A-Z0-9_]+=[A-Za-z0-9_+-]+',value), f'Invalid candidate define: {value}'
 name,setting=value.split('=',1);overrides[name]=setting
base=[f for f in (P/'flags.txt').read_text().split() if not f.startswith('-DFELUCCA_ID=')]
base+=[f'-DFELUCCA_ID="{identity["product"]}"']+[f for f in native['extra_flags'] if f.startswith('-D')]
flags=[f for f in base if not (f.startswith('-D') and f[2:].split('=',1)[0] in overrides)]
flags += [f'-D{name}={setting}' for name,setting in sorted(overrides.items())]
inc=['-I'+str(p)for p in [P/'shim',S/'firmware/hal',S/'firmware/src',S/'build/gen',S/'firmware/src/bb',S/'firmware/src/bb/x0x']];commands=[]
git=subprocess.run(['git','rev-parse','--is-inside-work-tree'],cwd=S,capture_output=True,text=True)
if git.returncode==0:
 tracked=subprocess.run(['git','ls-files'],cwd=S,capture_output=True,text=True,check=True).stdout.splitlines()
 source_files=sorted(f for f in tracked if f.startswith(('build/','firmware/')))
 commit=subprocess.run(['git','rev-parse','HEAD'],cwd=S,capture_output=True,text=True,check=True).stdout.strip()
else:
 source_files=sorted(str(p.relative_to(S)) for d in ('build','firmware') for p in (S/d).rglob('*') if p.is_file())
 commit=None
source_lines=[f'{f} {hashlib.sha256((S/f).read_bytes()).hexdigest()}' for f in source_files]
source_tree=hashlib.sha256('\n'.join(source_lines).encode()).hexdigest()
emulator_inputs=[P/n for n in ('compile.py','host_prep.py','emu-release.c','wasm_host.c','flags.txt','link.sh')]
emulator_inputs+=sorted(p for p in (P/'shim').rglob('*') if p.is_file())
emulator_lines=[f'{p.relative_to(S)} {hashlib.sha256(p.read_bytes()).hexdigest()}' for p in emulator_inputs]
emulator_tree=hashlib.sha256('\n'.join(emulator_lines).encode()).hexdigest()
(B/'BUILD-PROFILE.json').write_text(json.dumps({'recipe':a.recipe,'recipe_path':str(recipe.relative_to(S)),
    'candidate_defines':overrides,'flags':flags,'source_tree_sha256':source_tree,
    'source_files':len(source_files),'source_commit':commit,
    'emulator_build_inputs_sha256':emulator_tree,'emulator_build_input_files':len(emulator_inputs)},indent=2)+'\n')
def run(cmd,name):
 commands.append(cmd);p=subprocess.run(cmd,capture_output=True,text=True);(B/(name+'.log')).write_text(p.stdout+p.stderr);(B/'COMMANDS.json').write_text(json.dumps(commands,indent=2)+'\n');assert p.returncode==0,(name,p.stderr[-6000:])
run(['clang','--target=wasm32','-ffreestanding','-E','-w',*inc,*flags,str(S/'firmware/src/felucca.c'),'-o',str(B/'felucca.i')],'preprocess')
adapter=(P/H).read_text().replace('@PRODUCT@',identity['product']).replace('@DISPLAY_NAME@',identity['display_name'])
(B/H).write_text(adapter)
run(['python3',str(P/'host_prep.py'),str(B/'felucca.i'),str(B/H),str(B/'felucca_emu.i')],'host-prep')
# host_prep appends the emulator adapter after the firmware was preprocessed.
# Repeat the exact feature flags here so adapter-side #if branches describe
# the same candidate instead of silently falling back to feature-off exports.
run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w',*flags,
     '-x','c','-c',str(B/'felucca_emu.i'),'-o',str(B/'felucca.o')],'unity')
pre=(B/'felucca.i').read_text();files=[]
for unit in native['link_order']:
 if unit=='main':cmd=[]
 else:
  command_unit='bb_echo' if unit in derived_units else unit
  cmd=json.loads((S/'big-beat/integration/build/cmds-r4-onepass'/(command_unit+'-o-command.json')).read_text())
  if unit in derived_units:cmd=[x.replace(command_unit,unit) for x in cmd]
 for path in cmd:
  if path.startswith('/work/source/') and path.endswith('.c'):
   f=S/path[len('/work/source/'):] ;files.append((f,cmd))

for f,cmd in files:
 if not f.is_file()or re.search(r'^# [0-9]+ "'+re.escape(str(f))+'"',pre,re.M):continue
 # Preserve target-local symbol ownership.  The split 909 renderer is built
 # from the same upstream engine as drum909.c, but the target command renames
 # its public entry points and makes both units share external PCM/tables.
 # Dropping those per-unit defines compiles successfully and then fails only at
 # the final link with duplicate engines, so copy this bounded macro family
 # from the pinned target command rather than inventing an emulator variant.
 unit_defs=[x for x in cmd if x.startswith('-Ddrum909_') or
            x in ('-DBB909_EXTERNAL_PCM=1','-DBB909_EXTERNAL_TABLES=1')]
 run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w',*inc,*flags,
      *unit_defs,'-c',str(f),'-o',str(B/'obj'/(f.stem+'.o'))],f.stem)
run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w','-c',str(P/'wasm_host.c'),'-o',str(B/'whost.o')],'wasm-host')
print('Objects compiled. Link with link.sh using wasm-ld-14; see README.md.')
