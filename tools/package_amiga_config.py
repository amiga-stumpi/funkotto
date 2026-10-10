#!/usr/bin/env python3
"""Package the verified Amiga HUNK and its exact source inputs, no firmware."""
import argparse
import hashlib
import json
from pathlib import Path
from package_firmware import write_verified_zip, ROOT


def main():
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();manifest=json.loads((a.build/'manifest.json').read_text())
    data=(a.build/'FunkOttoConfig').read_bytes()
    if len(data)!=manifest['size'] or hashlib.sha256(data).hexdigest()!=manifest['sha256']:
        raise ValueError('Amiga executable/manifest mismatch')
    if manifest.get('parallel_transport') is not False:raise ValueError('Expected local SIM build')
    entries={'FunkOttoConfig':data,'manifest.json':(a.build/'manifest.json').read_bytes(),
        'ANLEITUNG.txt':(ROOT/'amiga/config/README.md').read_bytes()}
    for name,expected in manifest['sources'].items():
        source=(ROOT/name).read_bytes()
        if hashlib.sha256(source).hexdigest()!=expected:raise ValueError('Source differs: '+name)
        entries['source/'+name]=source
    for name in ['docs/amiga-konfiguration-plan.md','protocol/CONFIG.md',
                 'tests/test_amiga_config.c','tests/run_config_amiga_emulation.py',
                 'tests/run_m2_amiga_emulation.py','tools/package_amiga_config.py',
                 'tools/package_firmware.py']:
        entries['source/'+name]=(ROOT/name).read_bytes()
    entries['QUELLEN.txt']=(
        'https://github.com/amiga-stumpi/funkotto\nSource: '+manifest['source_id']+
        '\nLocal SIM only. Real AmigaOS 1.3 test pending. No firmware update needed.\n'
        'Start: FunkOttoConfig SELFTEST, then FunkOttoConfig SIM\n').encode()
    digest=write_verified_zip(a.output,entries)
    print(f'{a.output} members={len(entries)} size={a.output.stat().st_size} sha256={digest}')


if __name__=='__main__':main()
