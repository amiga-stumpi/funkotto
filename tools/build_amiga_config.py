#!/usr/bin/env python3
"""Build the OS 1.3 configuration simulator; no SDK/libc or active CIA code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from build_amiga_diag import elf_to_hunk, ROOT

SOURCES = ['amiga/diag/os.S', 'amiga/diag/runtime.c',
           'amiga/config/os_io.S', 'amiga/config/main.c', 'amiga/config/client.c',
           'amiga/config/sim.c', 'amiga/config/selftest.c',
           'firmware/src/config_protocol.c', 'firmware/src/wifi_model.c']


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cc',default=os.environ.get('AMIGA_CC','m68k-linux-gnu-gcc-13'))
    p.add_argument('--prefix',default=os.environ.get('AMIGA_BINUTILS','m68k-linux-gnu-'))
    p.add_argument('--output',type=Path,default=ROOT/'build/amiga-config')
    p.add_argument('--source-id',default='development')
    a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9._-]+',a.source_id): raise ValueError('Invalid source ID')
    a.output.mkdir(parents=True,exist_ok=True)
    objects=[]
    for name in SOURCES:
        obj=a.output/(Path(name).name+'.o')
        # GCC otherwise merges adjacent byte stores at odd FOC1 offsets into
        # word/long stores. The 68000 emulator checks the resulting accesses.
        subprocess.run([a.cc,'-m68000','-msoft-float','-std=c11','-Os',
            '-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector',
            '-fno-asynchronous-unwind-tables','-fno-unwind-tables','-fno-common',
            '-fno-store-merging',
            '-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
            '-DFO_DIAG_SOURCE="'+a.source_id+'"',
            '-I'+str(ROOT/'amiga/diag/freestanding'),'-I'+str(ROOT/'amiga/diag'),
            '-I'+str(ROOT/'amiga/config'),'-I'+str(ROOT/'firmware/include'),
            '-ffile-prefix-map='+str(ROOT)+'=funkotto',
            '-c',str(ROOT/name),'-o',str(obj)],check=True)
        objects.append(str(obj))
    elf=a.output/'FunkOttoConfig.elf'
    subprocess.run([a.prefix+'ld','--emit-relocs','-T',str(ROOT/'amiga/diag/link.ld'),
        '-Map='+str(a.output/'FunkOttoConfig.map'),'-o',str(elf)]+objects,check=True)
    data,count=elf_to_hunk(elf.read_bytes())
    (a.output/'FunkOttoConfig').write_bytes(data)
    inputs=SOURCES+['amiga/config/client.h','amiga/config/sim.h','amiga/diag/os.h',
        'amiga/diag/link.ld','amiga/diag/freestanding/string.h',
        'firmware/include/funkotto/config_protocol.h',
        'firmware/include/funkotto/wifi_service.h','firmware/include/funkotto/wifi_model.h',
        'firmware/include/funkotto/raw_wire.h','firmware/include/funkotto/ethernet.h',
        'tools/build_amiga_config.py','tools/build_amiga_diag.py']
    manifest={'target':'68000, AmigaOS 1.3 CLI, local SIM only',
        'format':'single CODE hunk + RELOC32','source_id':a.source_id,
        'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'relocations':count,
        'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],
        'sources':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in inputs},
        'hardware_tested':False,'os13_tested':False,'parallel_transport':False}
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({k:v for k,v in manifest.items() if k!='sources'},indent=2))


if __name__=='__main__':main()
