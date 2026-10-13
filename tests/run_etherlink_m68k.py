#!/usr/bin/env python3
"""Run the same FOE1 lifecycle tests as freestanding 68000 code, without OS/ROM.
Native ASan tests cover every length/corruption; this uses selected boundaries.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import *
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_amiga_diag import elf_to_hunk, ROOT
from run_m2_amiga_emulation import load_hunk


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cc',default=os.environ.get('AMIGA_CC','m68k-linux-gnu-gcc-13'))
    p.add_argument('--prefix',default=os.environ.get('AMIGA_BINUTILS','m68k-linux-gnu-'))
    a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='foe1-m68k-') as tmp:
        d=Path(tmp)
        (d/'assert.h').write_text('void fo_fail(unsigned line) __attribute__((noreturn));\n'
                                '#define assert(x) ((x) ? (void)0 : fo_fail(__LINE__))\n')
        (d/'stdio.h').write_text('int puts(const char *s);\n')
        (d/'runtime.c').write_text('int puts(const char *s) { (void)s; return 0; }\n')
        (d/'start.S').write_text('''.section .text.start,"ax"
.globl _start
_start:
    move.l %sp,initial_sp
    jsr main
    rts
.text
.globl fo_fail
fo_fail:
    move.l 4(%sp),%d0
    move.l initial_sp,%sp
    rts
.data
initial_sp: .long 0
.section .note.GNU-stack,"",@progbits
''')
        sources=[d/'start.S',d/'runtime.c',ROOT/'amiga/diag/runtime.c',
                 ROOT/'tests/test_etherlink.c',ROOT/'protocol/etherlink.c',
                 ROOT/'firmware/src/ethernet.c']
        objects=[]
        for i,src in enumerate(sources):
            obj=d/f'{i}.o';objects.append(str(obj))
            subprocess.run([a.cc,'-m68000','-msoft-float','-std=c11','-Os',
                '-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector',
                '-fno-asynchronous-unwind-tables','-fno-unwind-tables','-fno-store-merging',
                '-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-DFO_EL_M68K_TEST=1',
                '-I'+str(d),'-I'+str(ROOT/'amiga/diag/freestanding'),
                '-I'+str(ROOT/'protocol'),'-I'+str(ROOT/'firmware/include'),
                '-c',str(src),'-o',str(obj)],check=True)
        elf=d/'test.elf'
        subprocess.run([a.prefix+'ld','--emit-relocs','-T',str(ROOT/'amiga/diag/link.ld'),
                        '-o',str(elf)]+objects,check=True)
        data,_=elf_to_hunk(elf.read_bytes())
        base=0x20000;stack=0x80000;stop=0x90000
        u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
        u.mem_map(base,0x80000);u.mem_write(base,bytes(load_hunk(data,base)))
        u.mem_write(stack,struct.pack('>I',stop));u.reg_write(UC_M68K_REG_A7,stack)
        lowest=stack
        def access(uc,kind,address,size,value,ctx):
            nonlocal lowest
            assert size==1 or not address&1, f'Unaligned access {address:x}, PC {u.reg_read(UC_M68K_REG_PC)-base:x}'
            if stack-0x10000<=address<stack:lowest=min(lowest,address)
        u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
        u.emu_start(base,stop,count=150000000)
        assert u.reg_read(UC_M68K_REG_PC)==stop, 'Did not terminate'
        assert u.reg_read(UC_M68K_REG_D0)==0, f'Assertion line {u.reg_read(UC_M68K_REG_D0)}'
        assert u.reg_read(UC_M68K_REG_A7)==stack+4 and stack-lowest<4096
        print(f'PASS: FOE1 68000 lifecycle/queue/replay/IRQ/CRC tests; test stack {stack-lowest} bytes; no OS or hardware.')


if __name__=='__main__':main()
