#!/usr/bin/env python3
from pathlib import Path
import sys,json,subprocess
src=Path(__file__).resolve().parents[3]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
fixture=src/'big-beat/integration/tests/oracle'
oracle=out/'oracle.c';oracle.write_text((fixture/'integration-480.c').read_text().replace('@SRC@',str(src)))
args=[x.replace('@SRC@',str(src)).replace('@OUT@',str(out/'test-section-owner')).replace('@ORACLE@',str(src/'big-beat/composition-20/tests/section_owner_test.c')) for x in json.loads((fixture/'compile.json').read_text())]
args += ['-DORACLE_C="'+str(oracle)+'"','-Werror=implicit-function-declaration']
(out/'compile.json').write_text(json.dumps(args,indent=2)+'\n')
p=subprocess.run(args,capture_output=True,text=True);(out/'BUILD.log').write_text(p.stdout+p.stderr)
print(p.stdout+p.stderr);sys.exit(p.returncode)
