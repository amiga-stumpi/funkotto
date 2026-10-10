#!/usr/bin/env python3
"""68000 HUNK execution with explicit V34 OS mocks, not a real Kickstart test."""
import argparse
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import *
from run_m2_amiga_emulation import load_hunk


def run(path,args,typed=b'',*,workbench=False,fail_open=False,fail_library=False,
        fail_write=False,signal=False):
    u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    u.mem_map(0,0x100000)
    base=0x20000;image=load_hunk(path.read_bytes(),base);u.mem_write(base,bytes(image))
    eb=0x10000;dos=0x12000;task=0x18000;stack=0x80000;stop=0x90000
    def put32(p,v):u.mem_write(p,struct.pack('>I',v))
    put32(4,eb);put32(task+172,0 if workbench else 1);put32(stack,stop)
    u.mem_write(0x85000,args.encode());u.reg_write(UC_M68K_REG_A7,stack)
    u.reg_write(UC_M68K_REG_A0,0x85000);u.reg_write(UC_M68K_REG_D0,len(args))
    preserved=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,
        UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,
        UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]
    for i,r in enumerate(preserved):u.reg_write(r,0x12345000+i)
    vectors={eb+x for x in (-294,-552,-414,-306,-384,-372,-378,-132)}
    vectors|={dos+x for x in (-60,-48,-30,-36,-42,-198)}
    for a in vectors:u.mem_write(a,b'\x4e\x75')
    log=[];cons=0;lib=0;read=0;replied=0;delays=0;lowest=stack
    def reg(r):return u.reg_read(r)
    def text(a):
        b=bytearray()
        while (v:=u.mem_read(a,1)[0]):b.append(v);a+=1
        return b.decode()
    def os_call(uc,address,size,ctx):
        nonlocal cons,lib,read,replied,delays
        assert address in vectors, 'Unexpected OS call '+hex(address)
        d0=reg(UC_M68K_REG_D0);d1=reg(UC_M68K_REG_D1)
        d2=reg(UC_M68K_REG_D2);d3=reg(UC_M68K_REG_D3);a1=reg(UC_M68K_REG_A1);result=0
        if address==eb-294:result=task
        elif address==eb-552:
            assert text(a1)=='dos.library' and d0==34
            if not fail_library:lib+=1;result=dos
        elif address==eb-414:assert a1==dos;lib-=1
        elif address==eb-306:result=4096 if signal else 0
        elif address==eb-372:result=0x19000
        elif address==eb-378:assert a1==0x19000;replied+=1
        elif address==dos-60:result=123
        elif address==dos-30:
            assert text(d1)=='RAW:0/12/640/180/FunkOttoConfig SIM' and d2==1005
            if not fail_open:cons+=1;result=456
        elif address==dos-36:assert d1==456;cons-=1;result=1
        elif address==dos-42:
            assert d1==456 and d3==1 and cons==1
            if read<len(typed):u.mem_write(d2,typed[read:read+1]);read+=1;result=1
        elif address==dos-48:
            assert d1 in (123,456) and d3<1024
            log.append(bytes(u.mem_read(d2,d3)).decode('ascii'))
            result=0xffffffff if fail_write else d3
        elif address==dos-198:assert d1==5;delays+=1
        for r in (UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1):u.reg_write(r,0xdead0000)
        u.reg_write(UC_M68K_REG_D0,result)
    u.hook_add(UC_HOOK_CODE,os_call,begin=0xf000,end=0x12fff)
    def memory(uc,access,address,size,value,ctx):
        nonlocal lowest
        assert size==1 or not address&1, 'Unaligned 68000 access '+hex(address)+' PC='+hex(reg(UC_M68K_REG_PC)-base)
        if stack-0x10000<=address<stack:lowest=min(lowest,address)
    u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory,begin=base,end=0x8ffff)
    # No CIA or custom chips are mapped: any access faults.
    u.emu_start(base,stop,count=20000000)
    assert reg(UC_M68K_REG_PC)==stop and reg(UC_M68K_REG_A7)==stack+4
    assert lib==0 and cons==0 and replied==(1 if workbench else 0)
    assert stack-lowest<4096, 'Program stack exceeds 4 KiB (OS overhead not modeled)'
    for i,r in enumerate(preserved):assert reg(r)==0x12345000+i
    return reg(UC_M68K_REG_D0),''.join(log),stack-lowest,bytes(u.mem_read(base,len(image))),delays


def main():
    p=argparse.ArgumentParser();p.add_argument('executable',type=Path);a=p.parse_args()
    cases=0;maximum=0
    def check(args,typed=b'',code=0,**kwargs):
        nonlocal cases,maximum
        result,out,depth,memory,delays=run(a.executable,args,typed,**kwargs)
        assert result==code,(result,out);cases+=1;maximum=max(maximum,depth)
        return out,memory,delays
    out,_,_=check('SELFTEST\n');assert 'PASS: local FOC1' in out;print(out.strip())
    secret=b'NoEcho-987654'
    script=(b'status\rscan\rset\rDemo WLAN\r'+secret+b'\rconnect\rsave\rYES\r'
            b'reboot\rstatus\rstatus\rerase\rYES\rreboot\rstatus\rquit\r')
    out,memory,delays=check('SIM\n',script)
    assert 'SIM scan count=3' in out and 'SSID=""' in out and out.count('SSID="Demo WLAN"')>=2
    assert 'state=LINK_UP' in out and 'stored=1' in out and 'flash_state=2 sequence=2' in out
    assert 'ERROR' not in out and secret.decode() not in out and secret not in memory and delays>0
    out,_,_=check('SIM',b'set\rDemo\rshort\rstatus\rquit\r')
    assert 'Invalid input; profile unchanged.' in out and 'configured=0' in out
    for bad in (b'x'*33,b'abc\x1b[Dxyz',b'abc\x9bDxyz'):
        out,_,_=check('SIM',b'set\r'+bad+b'\rstatus\rquit\r')
        assert 'Invalid input' in out and 'configured=0' in out
    out,memory,_=check('SIM',b'set\rDemo\r'+secret+b'x'*60+b'\rquit\r')
    assert 'Invalid input' in out and secret.decode() not in out and secret not in memory
    out,_,_=check('SIM',b'set\rDemX\bo\r12345678\rstatus\rsave\rNO\rquit\r')
    assert 'SSID="Demo"' in out and 'Cancelled.' in out and 'stored=0' in out
    for ssid_size,key_size in ((1,8),(31,62),(32,63)):
        ssid=b'S'*ssid_size;key=b'BoundaryPassword'[:key_size].ljust(key_size,b'9')
        out,memory,_=check('SIM',b'set\r'+ssid+b'\r'+key+b'\rconnect\rsave\rYES\rstatus\rquit\r')
        assert 'ERROR' not in out and 'stored=1' in out and ('SSID="'+ssid.decode()+'"') in out
        assert key.decode() not in out and key not in memory
    for ending in (b'\x03',b'\x04',b''):
        out,memory,_=check('SIM',b'set\rDemo\r'+secret+ending,code=5)
        assert secret.decode() not in out and secret not in memory
    check('SIM',code=5,signal=True)
    check('SIM',code=20,fail_open=True)
    check('SELFTEST',code=20,fail_library=True)
    check('SIM',code=20,fail_write=True)
    check('PARALLEL',code=10)
    check('SELFTEST',code=20,workbench=True)
    print(f'PASS: {cases} 68000 runs, max program stack {maximum} bytes; V34 OS mocked, no CIA access.')


if __name__=='__main__':main()
