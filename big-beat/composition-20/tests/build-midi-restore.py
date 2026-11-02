#!/usr/bin/env python3
"""Compile the real C engine/platform restore regression with ASan/UBSan."""
from pathlib import Path
import json,sys,subprocess
source=Path(__file__).resolve().parents[3];out=Path(sys.argv[1]).resolve();out.mkdir(exist_ok=False)
fixture=source/'big-beat/integration/tests/oracle';oracle=out/'oracle.c';oracle.write_text((fixture/'integration-480.c').read_text().replace('@SRC@',str(source)))
args=[x.replace('@SRC@',str(source)).replace('@ORACLE@',str(source/'big-beat/composition-20/tests/midi_restore_test.c')).replace('@OUT@',str(out/'test'))for x in json.loads((fixture/'compile.json').read_text())]
args+=['-DORACLE_C="'+str(oracle)+'"','-DBB_WITH_PROJECT_FILES=1','-DBB_WITH_LIVE_CAPTURE=1','-DBB_WITH_DILLO_KIT_VIEW=1','-DBB_TRIM_INACTIVE_STATE=1','-Werror=implicit-function-declaration']
(out/'compile.json').write_text(json.dumps(args,indent=2)+'\n');p=subprocess.run(args,capture_output=True,text=True);(out/'BUILD.log').write_text(p.stdout+p.stderr);print((p.stdout+p.stderr)[-2000:]);sys.exit(p.returncode)
