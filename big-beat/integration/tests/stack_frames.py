import re,sys,json
def allocated(line):
    if re.fullmatch(r'\[--sp\] = (?:r\d+|rets|reti|psr|icfg|usp|ssp)',line):return 4
    m=re.fullmatch(r'\[--sp\] = \{([^}]+)\}',line)
    if m:
        c=0
        for r in m[1].split(','):
            s=re.fullmatch(r'r(\d+)-r(\d+)',r.strip()); c+=abs(int(s[1])-int(s[2]))+1 if s else 1
        return c*4
    m=re.fullmatch(r'sp \+= -(\d+)',line); return int(m[1]) if m else 0
def frames(path):
    out={};cur=None;pro=True
    for l in open(path):
        m=re.match(r'^([A-Za-z_.$][\w.$]*):\s*$',l)
        if m: cur=m[1];out[cur]=0;pro=True;continue
        if cur is None:continue
        p=l.split('\t')
        if len(p)<2:continue
        ins=p[-1].split(';')[0].strip()
        if pro:
            a=allocated(ins)
            if a: out[cur]+=a
            else: pro=False
    return out
a=frames(sys.argv[1]);b=frames(sys.argv[2])
new=sorted(((b[f],f) for f in b if f not in a),reverse=True)
grew=sorted(((b[f]-a[f],f,a[f],b[f]) for f in b if f in a and b[f]>a[f]),reverse=True)
print("functions",len(a),len(b),"| new:",len(new),"| larger frame:",len(grew))
print("largest new frames:",new[:12]); print("grown:",grew[:12])
print("largest frames overall (candidate):",sorted(((v,k) for k,v in b.items()),reverse=True)[:8])

def graph(path):
    calls={};cur=None;ind={}
    for l in open(path):
        m=re.match(r'^([A-Za-z_.$][\w.$]*):\s*$',l)
        if m: cur=m[1];calls[cur]=set();ind[cur]=0;continue
        if cur is None:continue
        m=re.search(r'\tcall -?\d+ <([\w.$]+)',l)
        if m: calls[cur].add(m[1].split('+')[0]); continue
        if re.search(r'\tcall r\d+|\tcall \[',l): ind[cur]+=1
    return calls,ind
import functools,sys
for name,path in (('boot42',sys.argv[1]),('candidate',sys.argv[2])):
    fr=frames(path);calls,ind=graph(path)
    @functools.lru_cache(None)
    def depth(f,stack=()):
        if f in stack:return (0,(f+'(cycle)',))
        best=(0,())
        for g in calls.get(f,()):
            d=depth(g,stack+(f,))
            if d[0]>best[0]:best=d
        return (fr.get(f,0)+best[0],(f,)+best[1])
    for root in sys.argv[3:]:
        d=depth(root); print(f"{name}: {root} worst direct-call stack {d[0]} B via {' > '.join(d[1][:8])}; indirect sites {sum(ind.get(f,0) for f in d[1])}")
    d=depth("fm1_main")
    print(f"{name}: worst direct-call stack from fm1_main = {d[0]} B via {' > '.join(d[1][:9])}")
    print(f"   indirect call sites reachable on that path: {sum(ind.get(f,0) for f in d[1])}")
