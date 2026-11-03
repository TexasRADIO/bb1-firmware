from pathlib import Path
import subprocess,json,shlex,re,os,sys
sys.dont_write_bytecode=True
import argparse
ap=argparse.ArgumentParser(description='Build the BB1 browser module from the selected 2.1 source and included adapter.')
ap.add_argument('source',type=Path)
ap.add_argument('output',type=Path)
a=ap.parse_args()
R=Path(__file__).resolve().parent;S=a.source.resolve();P=R;H='emu-release.c';B=a.output.resolve()
assert not B.exists(), 'Refuse output reuse'
B.mkdir(parents=True);(B/'obj').mkdir()

native=json.loads((S/'big-beat/integration/build/recipes/composition-21/recipe.json').read_text())
identity=json.loads((S/'big-beat/identity.json').read_text())
flags=[f for f in (P/'flags.txt').read_text().split() if not f.startswith('-DFELUCCA_ID=')]+[f'-DFELUCCA_ID="{identity["product"]}"']+[f for f in native['extra_flags'] if f.startswith('-D')];inc=['-I'+str(p)for p in [P/'shim',S/'firmware/hal',S/'firmware/src',S/'build/gen',S/'firmware/src/bb',S/'firmware/src/bb/x0x']];commands=[]
def run(cmd,name):
 commands.append(cmd);p=subprocess.run(cmd,capture_output=True,text=True);(B/(name+'.log')).write_text(p.stdout+p.stderr);(B/'COMMANDS.json').write_text(json.dumps(commands,indent=2)+'\n');assert p.returncode==0,(name,p.stderr[-6000:])
run(['clang','--target=wasm32','-ffreestanding','-E','-w',*inc,*flags,str(S/'firmware/src/felucca.c'),'-o',str(B/'felucca.i')],'preprocess')
adapter=(P/H).read_text().replace('@PRODUCT@',identity['product']).replace('@DISPLAY_NAME@',identity['display_name'])
(B/H).write_text(adapter)
run(['python3',str(P/'host_prep.py'),str(B/'felucca.i'),str(B/H),str(B/'felucca_emu.i')],'host-prep')
run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w','-c',str(B/'felucca_emu.i'),'-o',str(B/'felucca.o')],'unity')
pre=(B/'felucca.i').read_text();files=[]
for unit in native['link_order']:
 cmd=json.loads((S/'big-beat/integration/build/cmds-r4-onepass'/(unit+'-o-command.json')).read_text()) if unit!='main' else []
 for path in cmd:
  if path.startswith('/work/source/') and path.endswith('.c'):
   f=S/path[len('/work/source/'):] ;files.append(f)

for f in files:
 if not f.is_file()or re.search(r'^# [0-9]+ "'+re.escape(str(f))+'"',pre,re.M):continue
 run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w',*inc,*flags,*(['-DBB909_EXTERNAL_PCM=1'] if f.name=='drum909.c' else []),'-c',str(f),'-o',str(B/'obj'/(f.stem+'.o'))],f.stem)
run(['clang','--target=wasm32','-ffreestanding','-O2','-ffp-contract=off','-w','-c',str(P/'wasm_host.c'),'-o',str(B/'whost.o')],'wasm-host')
print('Objects compiled. Link with link.sh using wasm-ld-14; see README.md.')
