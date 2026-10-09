#!/usr/bin/env python3
"""Build freestanding 68000 ELF, then a single relocatable Amiga HUNK executable.

No Linux runtime or Amiga SDK redistribution is involved. Every relocation is
validated; unsupported ELF features fail rather than yielding a corrupt hunk.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def elf_to_hunk(data):
    if data[:7] != b'\x7fELF\x01\x02\x01':
        raise ValueError('Expected ELF32 big-endian')
    hdr = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    typ, machine, version, entry, _, shoff, _, _, _, _, shsize, shnum, _ = hdr
    if (typ, machine, version, entry, shsize) != (2, 4, 1, 0, 40):
        raise ValueError('Expected linked m68k ELF at entry zero')
    sections = [struct.unpack_from('>10I', data, shoff+i*shsize) for i in range(shnum)]
    allocated = [(i, s) for i, s in enumerate(sections) if s[2] & 2 and s[5]]
    if len(allocated) != 1:
        raise ValueError('Expected exactly one allocated section')
    image_index, s = allocated[0]
    if s[1] != 1 or s[3] != 0 or s[5] % 4 or s[2] & 6 != 6:
        raise ValueError('Image must be executable PROGBITS at zero, longword aligned')
    image = data[s[4]:s[4]+s[5]]
    if len(image) != s[5]:
        raise ValueError('Truncated image')
    relocations = []
    for section in sections:
        if section[1] not in (4, 9):
            continue
        if section[7] != image_index:
            raise ValueError('Relocation outside image')
        if section[1] != 4 or section[9] != 12:
            raise ValueError('Expected RELA entries')
        symbols = sections[section[6]]
        for off in range(section[4], section[4]+section[5], 12):
            address, info, _ = struct.unpack_from('>IIi', data, off)
            sym = struct.unpack_from('>IIIBBH', data, symbols[4]+(info>>8)*16)
            rtype = info & 255
            if sym[5] != image_index:
                raise ValueError('Undefined/external/absolute relocation')
            width = 4 if rtype in (1, 4) else 2 if rtype == 5 else 0
            if not width or address % 2 or address+width > len(image):
                raise ValueError('Unsupported relocation: '+str(rtype))
            if rtype == 1:
                value = struct.unpack_from('>I', image, address)[0]
                if value > len(image):
                    raise ValueError('Absolute pointer outside image')
                relocations.append(address)
    if len(set(relocations)) != len(relocations):
        raise ValueError('Duplicate relocation')
    words = [1011, 0, 1, 0, 0, len(image)//4, 1001, len(image)//4]
    hunk = struct.pack('>'+str(len(words))+'I', *words)+image
    if relocations:
        words = [1004, len(relocations), 0]+sorted(relocations)+[0]
        hunk += struct.pack('>'+str(len(words))+'I', *words)
    hunk += struct.pack('>I', 1010)
    return hunk, len(relocations)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--cc', default=os.environ.get('AMIGA_CC', 'm68k-linux-gnu-gcc-13'))
    p.add_argument('--prefix', default=os.environ.get('AMIGA_BINUTILS', 'm68k-linux-gnu-'))
    p.add_argument('--output', type=Path, default=ROOT/'build/amiga-m2')
    p.add_argument('--source-id', default='development')
    args = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9._-]+',args.source_id):
        raise ValueError('Invalid source ID')
    args.output.mkdir(parents=True, exist_ok=True)
    sources = ['amiga/diag/os.S', 'amiga/diag/main.c', 'amiga/diag/runtime.c',
               'protocol/m2.c', 'protocol/m2_host.c']
    objects = []
    for name in sources:
        obj = args.output/(Path(name).name+'.o')
        subprocess.run([args.cc, '-m68000', '-msoft-float', '-std=c11', '-Os',
                        '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-pie',
                        '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
                        '-fno-unwind-tables', '-Wall', '-Wextra', '-Werror',
                        '-Wconversion', '-Wshadow', '-fno-common',
                        '-DFO_DIAG_SOURCE="'+args.source_id+'"',
                        '-I'+str(ROOT/'amiga/diag/freestanding'), '-I'+str(ROOT/'protocol'),
                        '-ffile-prefix-map='+str(ROOT)+'=funkotto',
                        '-c', str(ROOT/name), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf = args.output/'FunkOttoDiag.elf'
    subprocess.run([args.prefix+'ld', '--emit-relocs', '-T', str(ROOT/'amiga/diag/link.ld'),
                    '-Map='+str(args.output/'FunkOttoDiag.map'), '-o', str(elf)]+objects, check=True)
    data, count = elf_to_hunk(elf.read_bytes())
    (args.output/'FunkOttoDiag').write_bytes(data)
    manifest = {'target': '68000, AmigaOS 1.3 CLI', 'format': 'single CODE hunk + RELOC32',
                'source_id': args.source_id,
                'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                'relocations': count, 'compiler': subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
                'sources': {n: hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sources},
                'hardware_tested': False, 'os13_tested': False}
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
