#!/usr/bin/env python3
"""Audit native object-section reachability; preserves every recipe qualification root.

Run against fresh objects from the matching recipe. This is a conservative link
analysis, not a runtime or whole-stack proof. Never remove IRQ/startup/KEEP
protections based on this report alone.
"""
from pathlib import Path
import struct,json,collections,argparse
root=Path(__file__).resolve().parent;parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True);parser.add_argument('--recipe',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();base=args.build
def elf(path):
 data=path.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',data);assert h[0][:6]==b'\x7fELF\x01\x01';assert h[11]==40
 sec=[dict(zip(('name_i','type','flags','addr','offset','size','link','info','align','entsize'),struct.unpack_from('<10I',data,h[6]+40*i))) for i in range(h[12])]
 def content(s):return data[s['offset']:s['offset']+s['size']]
 def name(blob,n):return blob[n:blob.find(b'\0',n)].decode('utf-8',errors='replace')
 strings=content(sec[h[13]])
 for i,s in enumerate(sec):s.update(name=name(strings,s['name_i']),index=i)
 sym=[]
 for s in sec:
  if s['type']!=2:continue
  names=content(sec[s['link']]);assert s['entsize']==16
  for at in range(s['offset'],s['offset']+s['size'],16):
   n,v,z,info,other,idx=struct.unpack_from('<IIIBBH',data,at);sym.append(dict(name=name(names,n),value=v,size=z,binding=info>>4,kind=info&15,section=idx))
 reloc=[]
 for s in sec:
  if s['type'] not in (4,9):continue
  assert s['entsize']==(12 if s['type']==4 else 8)
  for at in range(s['offset'],s['offset']+s['size'],s['entsize']):
   off,info=struct.unpack_from('<II',data,at);idx=info>>8
   assert idx<len(sym),(path.name,s['name'],hex(info),idx,len(sym))
   reloc.append((s['info'],idx,off,info&255))
 return sec,sym,reloc
recipe=json.loads(args.recipe.read_text())
objects={p.name:elf(p) for p in sorted(base.glob('*.o'))}
assert set(objects)=={name+'.o' for name in recipe['link_order']}, 'Object directory must contain exactly the matching recipe units'
definitions=collections.defaultdict(list);nodes={};edges=collections.defaultdict(set);external=collections.Counter()
for obj,(sections,symbols,relocs) in objects.items():
 for s in sections:
  if s['flags']&2:nodes[(obj,s['index'])]=s
 for s in symbols:
  if s['binding'] in (1,2) and 0<s['section']<len(sections):definitions[s['name']].append((obj,s['section']))
for obj,(sections,symbols,relocs) in objects.items():
 for src,idx,off,kind in relocs:
  n=(obj,src)
  if n not in nodes:continue
  s=symbols[idx];target=(obj,s['section'])
  if target in nodes:edges[n].add(target)
  elif s['section']==0:
   choices=definitions.get(s['name'],[])
   if choices:edges[n].update(choices)
   else:external[s['name']]+=1
# Keep the exact original linker protections for RAM/IRQ/startup/static storage.
keep=[]
for node,s in nodes.items():
 n=s['name']
 if n=='.text.crt0' or n.startswith(('.ram_text','.c1_text','.data','.sdata','.bss','.sbss','.pool','.noinit','.crash_context')):keep.append(node)
def reach(roots):
 seen=set();todo=list(roots)
 while todo:
  n=todo.pop()
  if n not in seen:seen.add(n);todo.extend(edges[n]-seen)
 return seen
natural=reach(keep)
qualification=recipe['roots']
kept=reach(keep+[n for f in qualification for n in definitions[f]])
all_reach=reach(keep+[n for f in recipe['roots'] for n in definitions[f]])
forced=[]
for f in recipe['roots']:
 ns=definitions[f];assert ns,f
 forced.append({'root':f,'natural_reachable':any(n in natural for n in ns),'qualification_required':f in qualification,'new_code_bytes_vs_required':sum(nodes[n]['size'] for n in reach(ns)-kept if nodes[n]['name'].startswith(('.text','.rodata','.ram_text','.c1_text')))})
unused_modules=[]
for obj in objects:
 rs=[n for n in kept if n[0]==obj and nodes[n]['size']]
 if not rs:unused_modules.append(obj[:-2])
# Report which protected static sections are retained only by linker protection.
roots_code=[n for n in keep if nodes[n]['name'].startswith(('.text','.ram_text','.c1_text'))]+[n for f in qualification for n in definitions[f]]
code_reach=reach(roots_code)
orphan_static=[{'object':n[0],'section':nodes[n]['name'],'bytes':nodes[n]['size']} for n in keep if n not in code_reach and nodes[n]['size']]
r={'objects':len(objects),'alloc_sections':len(nodes),'relocations':sum(len(x[2]) for x in objects.values()),'invalid_relocation_symbol_indices':0,'forced_roots':forced,'qualification_roots':qualification,'zero_retained_modules':unused_modules,'code_bytes_beyond_recipe_roots':sum(nodes[n]['size'] for n in all_reach-kept if nodes[n]['name'].startswith(('.text','.rodata','.ram_text','.c1_text'))),'protected_static_not_reached_by_code':orphan_static,'external_relocations':dict(external),'scope':'Conservative object-section relocation graph; preserves all exact KEEP protections and named qualification APIs. Link-map and final relink still required; not execution/timing proof.'}
args.output.write_text(json.dumps(r,indent=2)+'\n');

entry = [n for n,s in nodes.items() if s['name']=='.text.crt0' or n[0] in ('fm1_vec.o','fm1_isr.o')]
strict = reach(entry + [n for f in qualification for n in definitions[f]])
ram_unreachable = [{'object':n[0],'section':s['name'],'bytes':s['size']} for n,s in nodes.items() if n not in strict and s['size'] and s['name'].startswith(('.ram_text','.c1_text'))]
r['unreferenced_fast_memory_sections_conservative_review_only']=ram_unreachable
args.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({'objects':len(objects),'alloc_sections':len(nodes),'unreferenced_fast_memory_sections':len(ram_unreachable),'zero_retained_modules':unused_modules},indent=2))
