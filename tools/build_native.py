"""Fresh Windows x64 MinGW-compatible build; no assets or historical objects.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Explicit compilation inventory: never glob object files or generated engine data.
CORE = '''cfb codec containers crt_vc dsp effect fe_altpros fe_init fe_input
fe_lex fe_lexer fe_out fe_output fe_phones fe_phrase fe_procs fe_reader fe_rules
fe_small fe_split fe_text fe_vm fe_word fft lpc queue senone synload tags unit
unitsel userlex vcrt vload voices'''.split()
OVERRIDES = {'crt_vc', 'fe_init', 'fe_phrase', 'synload', 'vcrt'}
CLIENTS = ['standalone_test', 'lifecycle_test', 'live_capacity_test',
           'shutdown_test', 'failed_setup_test', 'sample_client', 'unload_probe']

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    p.add_argument('--output', type=Path, required=True, help='New, nonexistent output directory')
    a = p.parse_args()
    out = a.output.resolve()
    if out.exists():
        p.error('output must not exist: every object must be freshly compiled')
    cc = shutil.which(a.cc)
    if not cc:
        p.error('compiler not found: use --cc or CC with x64 MinGW gcc/compatible clang')
    out.mkdir(parents=True)
    commands = []
    flags = ['-std=c11', '-O2', '-g', '-ffp-contract=off', '-fno-builtin',
             '-ffile-prefix-map='+str(ROOT)+'=.',
             '-Inative', '-Isrc', '-Iport', '-Ilib']
    exports = re.findall(r'^\s+(s4_\w+)\s*$', (ROOT/'native/sapi4tts.def').read_text(), re.M)
    excluded = {'s4_abi_version', 's4_set_volume', 's4_get_volume', 's4_shutdown', 's4_speak_fragments'}
    renames = ['-D'+n+'=core_'+n for n in exports if n not in excluded]
    sources = ['native/core/sapi4tts.c', 'lib/stubs.c', 'native/core/dllimage.c']
    sources += [('native/core/' if n in OVERRIDES else 'src/')+n+'.c' for n in CORE]
    sources += ['native/boundary.c', 'native/ownership.c', 'native/shutdown.c']
    with (out/'build.log').open('w') as log:
        def run(args):
            cmd = list(map(str, args))
            commands.append(cmd)
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
        objects = []
        for source in sources:
            obj = out/(source.replace('/', '_')+'.o')
            run([cc, *flags, *(renames if source == sources[0] else []), '-c', source, '-o', obj])
            objects.append(obj)
        def link(dll, objs):
            run([cc, '-shared', '-static', '-Wl,--no-insert-timestamp,--image-base,0x180000000', '-o', dll,
                 *objs, 'native/sapi4tts.def', '-lm', '-lpthread', '-lbcrypt'])
        dll = out/'MSTTSSapi4.dll'
        link(dll, objects)
        # Separate fault-injection image; production DLL has no S4_TESTING hooks.
        fault = out/'fault_core.o'
        run([cc, *flags, *renames, '-DS4_TESTING', '-c', sources[0], '-o', fault])
        link(out/'fault.dll', [fault, *objects[1:]])
        for name in CLIENTS:
            run([cc, '-std=c11', '-O2', '-static', '-Inative', 'tests/native/'+name+'.c',
                 '-lpsapi', '-o', out/(name+'.exe')])
        for name in ('api_check', 'api_extras'):
            text = (ROOT/'tools'/f'{name}.c').read_text().replace('#include "sapi4tts.h"', '#include "dynamic_api.h"')
            text = text.replace('int main(int argc, char **argv) {',
                                'int main(int argc, char **argv) {\nif(!s4_load(getenv("S4_PREVIEW_DLL"))) return 2;')
            if name == 'api_extras':
                text = text.replace('while ((x = s4_open(argv[2], "Sam", err, sizeof err)) != NULL && opened < 200)',
                                    'while (opened < 200 && (x = s4_open(argv[2], "Sam", err, sizeof err)) != NULL)')
                text = text.replace('ok(x == NULL && err[0], "opening past the process\'s limit fails with a message");',
                                    'ok(opened == 200, "200 engines reuse slots without lifetime exhaustion");')
            generated = out/(name+'_dynamic.c')
            generated.write_text(text)
            run([cc, '-std=c11', '-O2', '-static', '-Inative', generated, '-lpthread', '-o', out/(name+'_dynamic.exe')])
    # Local diagnostic records are deliberately not portable/public artifacts.
    (out/'commands.json').write_text(json.dumps(commands, indent=2))
    report = dict(sources=sources, fresh_objects=len(objects), production_fault_hooks=False,
                  dll_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(), compiler=cc)
    (out/'build.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))

if __name__ == '__main__':
    main()
