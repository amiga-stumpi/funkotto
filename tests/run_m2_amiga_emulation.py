#!/usr/bin/env python3
"""Execute the delivered HUNK on a 68000 with explicitly mocked V34 OS vectors.
This checks instruction/ABI/cleanup paths, NOT Kickstart/OS or electrical timing.
Requires unicorn==2.1.4. No ROM is used or distributed.
"""
import argparse
from pathlib import Path
import struct
import ctypes
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import *


def load_hunk(data, base):
    words = list(struct.unpack('>'+str(len(data)//4)+'I', data))
    assert words[:5] == [1011, 0, 1, 0, 0] and words[6] == 1001
    size = words[7]*4
    assert words[5]*4 == size
    image = bytearray(data[32:32+size]);p = 8+size//4
    if words[p] == 1004:
        p += 1
        while words[p]:
            count, target = words[p:p+2];assert target == 0;p += 2
            for offset in words[p:p+count]:
                assert offset % 2 == 0 and offset+4 <= size
                value = struct.unpack_from('>I', image, offset)[0]
                struct.pack_into('>I', image, offset, value+base)
            p += count
        p += 1
    assert words[p:] == [1010]
    return image


def run(path, args, *, busy=None, timer=False, cancel=False, workbench=False, adapter=None):
    u=Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    u.mem_map(0, 0x100000);u.mem_map(0xbfc000,0x4000)
    base=0x20000;u.mem_write(base, bytes(load_hunk(path.read_bytes(),base)))
    execbase=0x10000;dos=0x12000;misc=0x14000;task=0x18000;stop=0x90000;stack=0x80000
    u.mem_write(4,struct.pack('>I',execbase));u.mem_write(task+172,struct.pack('>I',0 if workbench else 1))
    u.mem_write(stack,struct.pack('>I',stop));u.mem_write(0x85000,args.encode())
    u.reg_write(UC_M68K_REG_A7,stack);u.reg_write(UC_M68K_REG_A0,0x85000);u.reg_write(UC_M68K_REG_D0,len(args))
    nonvolatile=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,
                 UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,
                 UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]
    for i,r in enumerate(nonvolatile):u.reg_write(r,0x12345000+i)
    u.mem_write(0xbfd000,b'\xfb');u.mem_write(0xbfd200,b'\xe0')
    u.mem_write(0xbfe301,b'\xff');u.mem_write(0xbfe101,b'\x55')
    if timer:u.mem_write(0xbfee01,b'\x02')
    log=[];alloc=[];freed=[];cia=[];clock=0;disable_depth=0;lowest_stack=stack
    phase='wait';wire_busy=1;wire_pout=0;wire_request=bytearray();wire_reply=b'';wire_read=0
    def reg(r):return u.reg_read(r)
    def cstr(a):
        b=bytearray()
        while (x:=u.mem_read(a,1)[0]):b.append(x);a+=1
        return b.decode()
    vectors={execbase+n for n in (-294,-552,-414,-498,-120,-126,-306,-384,-372,-378,-132)}
    vectors|={dos-60,dos-48,misc-6,misc-12}
    for address in vectors:u.mem_write(address,b'\x4e\x75')
    def os_call(uc,address,size,ctx):
        nonlocal disable_depth
        if address not in vectors:raise AssertionError('Unexpected OS vector '+hex(address))
        d0=reg(UC_M68K_REG_D0);a1=reg(UC_M68K_REG_A1);result=0
        if address==execbase-294:result=task
        elif address==execbase-552:assert cstr(a1)=='dos.library' and d0==34;result=dos
        elif address==execbase-498:assert cstr(a1)=='misc.resource';result=misc
        elif address==execbase-414:assert a1==dos
        elif address==execbase-120:disable_depth+=1
        elif address==execbase-126:disable_depth-=1;assert disable_depth>=0
        elif address==execbase-306:result=4096 if cancel else 0
        elif address==dos-60:result=123
        elif address==dos-48:
            assert reg(UC_M68K_REG_D1)==123
            length=reg(UC_M68K_REG_D3);assert length<1000
            log.append(bytes(u.mem_read(reg(UC_M68K_REG_D2),length)).decode());result=length
        elif address==misc-6:
            assert d0 in (2,3) and cstr(a1)=='FunkOttoDiag'
            if d0==busy:result=0x88000
            else:alloc.append(d0)
        elif address==misc-12:freed.append(d0)
        elif address==execbase-372:result=0x19000
        elif address==execbase-378:assert a1==0x19000
        # OS routines may clobber all volatile registers. Pointer-return wrappers
        # must move Amiga D0 to the compiler's A0 pointer return register.
        for r in (UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1):u.reg_write(r,0xdead0000)
        u.reg_write(UC_M68K_REG_D0,result)
    u.hook_add(UC_HOOK_CODE,os_call,begin=0xf000,end=0x14fff)
    def alignment(uc,access,address,size,value,ctx):
        nonlocal lowest_stack
        if size>1 and address&1:raise AssertionError('68000 odd word/long access '+hex(address))
        if stack-0x10000<=address<stack:lowest_stack=min(lowest_stack,address)
    u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,alignment,begin=base,end=0x8ffff)
    def cia_access(uc,access,address,size,value,ctx):
        nonlocal clock,phase,wire_busy,wire_pout,wire_request,wire_reply,wire_read
        assert size==1;cia.append((access,address,value))
        if adapter:
            if access==16 and address==0xbfd000:
                current=u.mem_read(address,1)[0]
                u.mem_write(address,bytes([(current&252)|wire_busy|(wire_pout<<1)]))
            elif access==17 and address==0xbfd000:
                high=bool(value&4)
                assert u.mem_read(0xbfe301,1)==b'\x00' or phase=='rx', 'host must release data before SEL'
                if high:
                    if len(wire_request)==1536:
                        wire_reply=adapter(bytes(wire_request));wire_read=0;phase='tx';wire_busy=0;wire_pout=1
                    else:phase='sync';wire_busy=1;wire_pout=1
                else:
                    phase='rx';wire_busy=0;wire_pout=0;wire_request=bytearray()
            elif access==17 and address==0xbfe301 and value:
                assert phase=='rx' and wire_pout==0, 'data bus contention'
            elif access==17 and address==0xbfe101 and phase=='rx':
                wire_request.append(value);wire_busy^=1
            elif access==16 and address==0xbfe101 and phase=='tx':
                assert u.mem_read(0xbfe301,1)==b'\x00'
                u.mem_write(address,wire_reply[wire_read:wire_read+1]);wire_read+=1;wire_busy^=1
        if access==16: # UC_MEM_READ
            if address==0xbfea01:
                clock=(clock+1)&0xffffff
                u.mem_write(0xbfea01,bytes([clock>>16]));u.mem_write(0xbfe901,bytes([(clock>>8)&255]));u.mem_write(0xbfe801,bytes([clock&255]))
    u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,cia_access,begin=0xbfd000,end=0xbfefff)
    u.emu_start(base,stop,count=100000000)
    assert reg(UC_M68K_REG_PC)==stop, 'did not terminate'
    assert reg(UC_M68K_REG_A7)==stack+4 and disable_depth==0
    assert stack-lowest_stack<4096, 'program stack exceeds 4 KiB (OS stack not modeled)'
    for i,r in enumerate(nonvolatile):assert reg(r)==0x12345000+i, 'nonvolatile register changed'
    assert sorted(alloc)==sorted(freed)
    return reg(UC_M68K_REG_D0),''.join(log),cia,u


def main():
    p=argparse.ArgumentParser();p.add_argument('executable',type=Path);a=p.parse_args()
    code,out,cia,_=run(a.executable,'SELFTEST\n');assert code==0 and 'PASS: local' in out and not cia
    print(out.strip())
    for kwargs in ({'busy':2},{'busy':3},{'timer':True},{},{'cancel':True}):
        code,out,cia,u=run(a.executable,'RUN M0-VERIFIED 1\n',**kwargs)
        assert code==20
        if not kwargs or kwargs.get('cancel'):assert u.mem_read(0xbfe301,1)==b'\x00'
    code,out,cia,_=run(a.executable,'RUN 1\n');assert code==20 and not cia
    code,out,cia,_=run(a.executable,'SELFTEST',workbench=True);assert code==20 and not cia
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory() as tmp:
        so=Path(tmp)/'protocol.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-I'+str(root/'protocol'),
                        str(root/'protocol/m2.c'),'-o',str(so)],check=True)
        lib=ctypes.CDLL(str(so));state=ctypes.create_string_buffer(8192)
        lib.fo_m2_init.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
        lib.fo_m2_request.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
        lib.fo_m2_request.restype=ctypes.c_bool
        lib.fo_m2_init(state,0x44330000)
        def adapter(request):
            reply=ctypes.create_string_buffer(1536)
            assert lib.fo_m2_request(state,request,reply)
            return reply.raw
        code,out,cia,_=run(a.executable,'RUN M0-VERIFIED 8\n',adapter=adapter)
        assert code==0 and 'PASS: transport' in out
        print(out.strip())
    print('68000 HUNK: SELFTEST, resource conflicts, timeout, cancellation, argument gate, Workbench reply and ABI checks passed (OS mocked).')


if __name__=='__main__':main()
