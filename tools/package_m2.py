#!/usr/bin/env python3
"""Deliver only the locked M2 build plus the verified Amiga diagnostic."""
import argparse
import hashlib
import json
from pathlib import Path
from package_firmware import write_verified_zip, ROOT


def main():
    p=argparse.ArgumentParser()
    p.add_argument('build',type=Path);p.add_argument('amiga',type=Path)
    p.add_argument('sdk',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--toolchain-docs',type=Path,default=Path('/usr/share/doc'))
    a=p.parse_args();entries={}
    manifest=json.loads((a.build/'funkotto_m2-manifest.json').read_text())
    amiga=json.loads((a.amiga/'manifest.json').read_text())
    if manifest.get('active_build') is not False or not manifest.get('boot_locked'):
        raise ValueError('Only locked M2 firmware may enter the standard download')
    if manifest['source_id'] != amiga['source_id']:
        raise ValueError('Firmware and Amiga sources differ')
    for name,expected in manifest['artifacts'].items():
        data=(a.build/name).read_bytes()
        if len(data)!=expected['size'] or hashlib.sha256(data).hexdigest()!=expected['sha256']:
            raise ValueError('Firmware artifact mismatch: '+name)
        entries[name]=data
    binary=(a.amiga/'FunkOttoDiag').read_bytes()
    if len(binary)!=amiga['size'] or hashlib.sha256(binary).hexdigest()!=amiga['sha256']:
        raise ValueError('Amiga artifact mismatch')
    entries['amiga/FunkOttoDiag']=binary
    entries['amiga/manifest.json']=(a.amiga/'manifest.json').read_bytes()
    entries['manifest.json']=(a.build/'funkotto_m2-manifest.json').read_bytes()
    entries['ANLEITUNG.md']=(ROOT/'firmware/M2_TEST.md').read_bytes()
    for name in ['firmware/M2_TEST.md','amiga/diag/README.md','protocol/M2.md',
                 'docs/results/2026-10-09-m2.md','docs/firmware-v0.1-plan.md',
                 'firmware/dependencies.json']:
        entries[name]=(ROOT/name).read_bytes()
    for name,path in {'pico-sdk.txt':'LICENSE.TXT','tinyusb.txt':'lib/tinyusb/LICENSE',
                      'pico-printf.txt':'src/rp2_common/pico_printf/LICENSE',
                      'cmsis.txt':'src/rp2_common/cmsis/stub/CMSIS/LICENSE.txt'}.items():
        entries['licenses/'+name]=(a.sdk/path).read_bytes()
    for name in ['libnewlib-arm-none-eabi','gcc-arm-none-eabi']:
        entries['licenses/'+name+'.txt']=(a.toolchain_docs/name/'copyright').read_bytes()
    entries['QUELLEN.txt']=(
        'https://github.com/amiga-stumpi/funkotto\nSource: '+manifest['source_id']+
        '\nM2 default image is locked. No WLAN. No hardware M2 acceptance yet.\n'
        'Start on Amiga: FunkOttoDiag SELFTEST (no adapter required).\n').encode()
    digest=write_verified_zip(a.output,entries)
    print(f'{a.output} members={len(entries)} sha256={digest}')


if __name__=='__main__':main()
