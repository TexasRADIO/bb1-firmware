#!/usr/bin/env python3
"""Independent stdlib wire inspection, not nested scene or live engine admission."""
from pathlib import Path
import hashlib,json,struct,sys,zlib
p=Path(sys.argv[1]);wire=p.read_bytes()
assert 64<=len(wire)<=32768 and wire[:4]==b'BBP2'
assert struct.unpack_from('<HH',wire,4)==(1,64)
size,crc,features=struct.unpack_from('<III',wire,8)
assert size==len(wire) and features==1 and not any(wire[56:64])
assert zlib.crc32(wire[:12]+wire[16:])==crc
cat,stamp,libh,libp,scene=struct.unpack_from('<IIIII',wire,20)
ns,nr,nc,nw=wire[40:44];assert ns<=32 and nr<=64 and nc<=96 and nw<=16
rev,nextid,nextwork=struct.unpack_from('<III',wire,44);assert rev and nextid and nextwork
assert 0<scene<=3840 and 64+scene<=len(wire)
at=64;end=len(wire)-scene

def take(n):
 global at
 assert 0<=n<=end-at
 out=wire[at:at+n];at+=n;return out

def word(n):return int.from_bytes(take(n),'little')
def part():return {'id':word(4),'revision':word(4),'source':word(1)}
def text():
 n=word(1);assert 0<n<96
 data=take(n);assert b'\0' not in data
 return data.decode('utf-8',errors='backslashreplace')

sections=[]
for _ in range(ns):
 ident=word(4);r=word(4);name=take(24);assert ident and ident<nextid and 0<r<=rev and b'\0' in name
 refs=[part() for _ in range(3)];duration=word(2);assert 0<duration<=4096
 sections.append({'id':ident,'name':name.split(b'\0')[0].decode('utf-8',errors='backslashreplace'),'parts':refs,'duration':duration})
ids={s['id'] for s in sections};assert len(ids)==ns
rows=[]
for _ in range(nr):
 ident=word(4);repeats=word(1);assert ident in ids and 0<repeats<=64
 rows.append({'section':ident,'repeats':repeats})
refs=[]
for _ in range(nc):
 bank,pt=text(),text();group,kind,bname,pname=[word(1) for _ in range(4)]
 assert group<3 and kind<2 and bname<2 and pname<2
 refs.append({'bank':bank,'part':pt,'group':group,'kind':kind,'bank_fallback':bname,'part_fallback':pname})
works=[]
for i in range(nw):
 r=word(4);origin=part();protected=word(8);kind=word(1);assert 0<r<nextwork and kind<2
 assert not works or r>works[-1]['revision']
 if not kind:
  length,count,lattice,bars=[word(1) for _ in range(4)]
  assert 0<length<=64 and count<=128 and lattice<=2 and bars<=4
  events=[word(2) for _ in range(count)]
  payload={'length':length,'count':count,'lattice':lattice,'bars':bars,'source_ordered_event_words':events}
 else:
  length=word(1);assert 0<length<=64
  notes=[]
  for _ in range(64):
   note,gate,accent,slide=[word(1) for _ in range(4)];expression=word(2)
   assert note<=127 and gate<=1 and accent<=1 and slide<=1
   notes.append([note,gate,accent,slide,expression])
  payload={'length':length,'notes_and_expression':notes}
 assert not protected>>length
 works.append({'revision':r,'origin':origin,'protected_steps':protected,'kind':kind,'payload':payload})
assert at==end

def valid_ref(r,kind,before):
 assert r['id'] and r['source'] in (1,2)
 if r['source']==1:
  assert r['id']<=nc and r['revision']==cat and refs[r['id']-1]['kind']==kind
 else:
  assert r['id']<=before and r['revision']==works[r['id']-1]['revision'] and works[r['id']-1]['kind']==kind
for i,w in enumerate(works):valid_ref(w['origin'],w['kind'],i)
for s in sections:
 for m,r in enumerate(s['parts']):
  if m==2 and not r['id']:assert not r['revision'] and not r['source']
  else:valid_ref(r,int(m==2),nw)
result={'pass':True,'scope':'Independent portable framing/reference/metadata inspection; nested scene admission and musical playback tested by C engine separately. Synthetic fixture, not owner saved data.','bytes':len(wire),'sha256':hashlib.sha256(wire).hexdigest(),'catalogue_crc':cat,'catalogue_stamp':stamp,'library_header_crc':libh,'library_payload_crc':libp,'counts':{'sections':ns,'rows':nr,'curated_references':nc,'working_parts':nw},'scene_bytes':scene,'nested_magic':wire[end:end+4].decode('ascii',errors='replace'),'metadata_consumed_exactly':True,'all_source_lineages_valid':True,'sections':sections,'rows':rows,'curated_references':refs,'working_parts':works}
if len(sys.argv)>2:Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('sections','rows','curated_references','working_parts')},indent=2))
