"""Audit staged allowlisted source, not merely file extensions.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
# Explicit publication inventory. Changes to this inventory require review.
ALLOWED={'.gitignore','.gitattributes','src/cfb.c','docs/NATIVE-NVDA.md',
         'LICENSES/GPL-2.0.txt','LICENSES/NVDA-NOTICE.md',
         'nvda/manifest.ini','nvda/synthDrivers/msttsSapi4.py','nvda/synthDrivers/_msttsSapi4Native.py',
         'tools/build_native.py','tools/prepare_runtime.py','tools/audit_staged.py'}
ALLOWED.update('native/'+n for n in ['boundary.c','ownership.c','ownership.h','shutdown.c','shutdown.h',
                'resource_hashes.h','sapi4tts.h','sapi4tts.def','dynamic_api.h','fragments.h','fragments_impl.h'])
ALLOWED.update('native/core/'+n+'.c' for n in ['crt_vc','dllimage','fe_init','fe_phrase','sapi4tts','synload','vcrt'])
ALLOWED.update('tests/native/'+n+'.c' for n in ['standalone_test','lifecycle_test','live_capacity_test',
                'shutdown_test','failed_setup_test','sample_client','unload_probe'])
ALLOWED.update('tests/native/'+n+'.py' for n in ['contract','verify','compare_baseline','fragments'])
ALLOWED.update('tests/driver/'+n+'.py' for n in ['bootstrap','exercise','test_regression','extra_checks','limits','negative_control','smoke22'])
ALLOWED.update('tests/fixtures/'+n+'.json' for n in ['golden46','original130'])

def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)
def main():
    paths=git('diff','--cached','--name-only','--diff-filter=ACMR','-z').decode().strip('\0').split('\0')
    paths=[p for p in paths if p];assert paths,'No staged files'
    errors=[];rows=[]
    patterns={
        'personal_home':r'(?i)(?:[A-Z]:[\\/]+Users[\\/]+|/home/|/Users/)',
        'credential':r'(?i)(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|-----BEGIN (?:RSA |OPENSSH )?PRIVATE KEY-----|AKIA[0-9A-Z]{16})',
        'historical_dependency':r'(?:tv-decomp|release-candidate/|artifacts/preview|research-sidecar|research-transparency)',
    }
    # Ignore declarations in the strict ignore file, not executable dependencies.
    for p in paths:
        if p not in ALLOWED:errors.append((p,'not allowlisted'))
        raw=git('show',':'+p)
        if b'\0' in raw or raw[:2]==b'MZ' or raw[:4]==b'PK\x03\x04':errors.append((p,'binary content'))
        try:text=raw.decode('utf8')
        except UnicodeDecodeError:errors.append((p,'not UTF-8 text'));continue
        lines=text.splitlines()
        if len(lines)>1000:errors.append((p,'over 1000 lines'))
        if len(raw)>200000:errors.append((p,'unexpected large source file'))
        for kind,pattern in patterns.items():
            # Scanner contains its own literal pattern examples, not credentials.
            if p=='tools/audit_staged.py':continue
            matches=[i for i,line in enumerate(lines,1) if re.search(pattern,line)]
            if p=='.gitignore' and kind=='historical_dependency':continue
            if matches:errors.append((p,kind,matches))
        rows.append(dict(path=p,bytes=len(raw),lines=len(lines),sha256=hashlib.sha256(raw).hexdigest()))
    expected_nvda={'nvda/manifest.ini','nvda/synthDrivers/msttsSapi4.py','nvda/synthDrivers/_msttsSapi4Native.py'}
    staged_nvda=set(git('ls-files','--','nvda').decode().splitlines())
    if staged_nvda and staged_nvda!=expected_nvda:errors.append(('nvda/','must contain manifest and exactly two Python modules'))
    subprocess.run(['git','diff','--cached','--check'],cwd=ROOT,check=True)
    report=dict(files=len(rows),total_lines=sum(r['lines'] for r in rows),max_lines=max(r['lines'] for r in rows),errors=errors,inventory=rows)
    output=ROOT/'.local/staged-audit.json';output.parent.mkdir(exist_ok=True);output.write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
    if errors:sys.exit(1)
if __name__=='__main__':main()
