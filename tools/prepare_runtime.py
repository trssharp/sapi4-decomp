"""Create an ignored local runtime tree, never an archive or publication.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dll', type=Path, required=True)
    p.add_argument('--addon-output', type=Path, required=True)
    p.add_argument('--resources-dir', type=Path)
    p.add_argument('--include-resources', action='store_true', help='Explicit private local copy of caller-owned assets')
    a = p.parse_args()
    out = a.addon_output.resolve()
    if out.exists():
        p.error('output must not exist')
    # Require a git-ignored local destination, even if the caller chooses another name.
    if not out.is_relative_to(ROOT) or subprocess.run(['git', 'check-ignore', '-q', str(out)], cwd=ROOT).returncode:
        p.error('addon output must be inside an ignored directory in this checkout')
    if a.include_resources and not a.resources_dir:
        p.error('--include-resources requires --resources-dir')
    hashes = dict(re.findall(r'\{"([^"]+)","([0-9a-f]{64})"\}', (ROOT/'native/resource_hashes.h').read_text()))
    if a.include_resources:
        for name, expected in hashes.items():
            if hashlib.sha256((a.resources_dir/name).read_bytes()).hexdigest() != expected:
                p.error('resource SHA256 mismatch: '+name)
    shutil.copytree(ROOT/'nvda', out, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    dest = out/'synthDrivers'
    shutil.copyfile(a.dll, dest/'MSTTSSapi4.dll')
    # Preserve exact 0.5.2 tracked modules. Only generated runtime pin changes
    # because a source rebuild can have different debug paths/compiler metadata.
    bridge = dest/'_msttsSapi4Native.py'
    text = bridge.read_text()
    text, count = re.subn(r"DLL_SHA256 = '[0-9a-f]{64}'", "DLL_SHA256 = '"+hashlib.sha256(a.dll.read_bytes()).hexdigest()+"'", text)
    assert count == 1
    bridge.write_text(text)
    shutil.copyfile(ROOT/'docs/NATIVE-NVDA.md', out/'readme.md')
    if a.include_resources:
        data = dest/'msttsSapi4Data'
        data.mkdir()
        for name in hashes:
            shutil.copyfile(a.resources_dir/name, data/name)
    print('Private local runtime prepared; resources copied:', a.include_resources)

if __name__ == '__main__':
    main()
