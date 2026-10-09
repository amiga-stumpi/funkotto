#!/usr/bin/env python3
"""Small instruction interpreter for the actual pioasm output used by M2.
Tests pin/STROBE order, first/last byte, FIFO backpressure and held BUSY.
It is not a timing model of GPIO synchronizers, DMA arbitration or CIA hardware.
"""
import argparse
from pathlib import Path
import re


class SM:
    def __init__(self, program, tx, count):
        self.code=program;self.tx=tx;self.pc=0;self.x=0;self.y=count-1
        self.isr=0;self.osr=0;self.fifo=[];self.irq=set();self.strobe=1
        self.data=0;self.busy=0

    def step(self):
        word=self.code[self.pc];op=word>>13;a=(word>>5)&7;n=word&31
        if self.tx and word&0x1000:self.busy=(word>>11)&1
        next_pc=(self.pc+1)%len(self.code)
        if op==0:
            assert a in (0,4)
            if a==0:next_pc=n
            else:
                if self.y:next_pc=n
                self.y=(self.y-1)&0xffffffff
        elif op==1:
            assert a&3==0 and n==8
            if self.strobe != ((word>>7)&1):return
        elif op==2:
            assert a==0 and n==8;self.isr=((self.isr<<8)|self.data)&0xffffffff
        elif op==3:
            assert a==0 and n==8;self.data=self.osr&255;self.osr>>=8
        elif op==4:
            if word&0x80:
                if not self.fifo:return
                self.osr=self.fifo.pop(0)
            else:
                if len(self.fifo)==4:return
                self.fifo.append(self.isr);self.isr=0
        elif op==5:
            source=word&7;invert=(word>>3)&3;assert source==1 and invert in (0,1)
            value=(~self.x)&0xffffffff if invert else self.x
            if a==1:self.x=value
            else:assert a==0;self.busy=value&1
        elif op==6:
            assert n in (0,1);self.irq.add(n)
        else:raise AssertionError(hex(word))
        self.pc=next_pc

    def cycles(self,n=40):
        for _ in range(n):self.step()


def check(header):
    text=header.read_text();programs={}
    for name,body in re.findall(r'fo_parallel_(rx|tx)_program_instructions\[\] = \{(.*?)\};',text,re.S):
        programs[name]=[int(x,16) for x in re.findall(r'^\s*(0x[0-9a-f]+),',body,re.M)]
    assert len(programs['rx'])+len(programs['tx'])<=32
    for count in (1,2,3,1536):
        data=[(i*73+0x55)&255 for i in range(count)]
        rx=SM(programs['rx'],False,count)
        for i,b in enumerate(data):
            rx.cycles();assert rx.busy==i&1
            rx.data=b;rx.strobe=0;rx.cycles();assert rx.busy==i&1
            rx.strobe=1;rx.cycles();assert rx.busy==(i+1)&1
            assert rx.fifo.pop(0)==b
        assert 1 in rx.irq
        pc=rx.pc;rx.strobe=0;rx.cycles();assert rx.pc==pc and not rx.fifo
        tx=SM(programs['tx'],True,count);tx.fifo=data[:4];sent=min(4,count)
        tx.cycles();assert 0 in tx.irq and tx.data==data[0] and tx.busy==0
        for i,b in enumerate(data):
            assert tx.data==b
            tx.cycles(100);assert tx.data==b and tx.busy==i&1
            tx.strobe=0;tx.cycles();assert tx.busy==i&1
            tx.strobe=1
            if sent<count and len(tx.fifo)<4:tx.fifo.append(data[sent]);sent+=1
            tx.cycles();assert tx.busy==(i+1)&1
        assert 1 in tx.irq and not tx.fifo and sent==count
    # RX stalled PUSH must not acknowledge or overwrite the pending sample.
    rx=SM(programs['rx'],False,6);rx.fifo=[1,2,3,4]
    rx.cycles();rx.data=0xaa;rx.strobe=0;rx.cycles();rx.strobe=1;rx.cycles()
    assert rx.busy==0 and rx.fifo==[1,2,3,4]
    rx.fifo.pop(0);rx.cycles();assert rx.busy==1 and rx.fifo[-1]==0xaa
    # TX must not acknowledge until next data arrives in a previously empty FIFO.
    tx=SM(programs['tx'],True,2);tx.fifo=[0x55];tx.cycles()
    tx.strobe=0;tx.cycles();tx.strobe=1;tx.cycles();assert tx.busy==0
    tx.fifo=[0xaa];tx.cycles();assert tx.busy==1 and tx.data==0xaa
    print('M2 pioasm instruction tests passed: 1/2/3/1536 bytes, held ACK, first/last byte, RX/TX FIFO stalls.')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('header',type=Path);check(p.parse_args().header)
