#include "m2.h"
#include "m2_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fo_m2_server s;
static uint8_t b[FO_M2_BLOCK],r[FO_M2_BLOCK],p[FO_M2_PAYLOAD],saved[FO_M2_BLOCK];
static uint32_t hello(void) {
    memset(p,0,8);p[7]=1;
    assert(fo_m2_pack(b,FO_M2_HELLO,0,p,8));assert(fo_m2_request(&s,b,r));
    struct fo_m2_frame f;assert(fo_m2_unpack(r,&f));assert(f.length==18);
    assert(!memcmp(f.payload+1,p,8));return fo_m2_u32(f.payload+9);
}
static void protocol(void) {
    assert(fo_m2_crc((const uint8_t *)"123456789",9)==0x29b1);
    fo_m2_init(&s,0xffffffffu);uint32_t session=hello();assert(session==1);
    memcpy(saved,r,sizeof(r));assert(fo_m2_request(&s,b,r));assert(!memcmp(saved,r,sizeof(r)));
    assert(s.replays==1 && s.accepted==1);
    fo_m2_put32(p,session);
    for(unsigned i=0;i<FO_M2_ECHO_MAX;i++) p[i+4]=(uint8_t)i;
    assert(fo_m2_pack(b,FO_M2_ECHO,1,p,1504));assert(fo_m2_request(&s,b,r));
    struct fo_m2_frame f;assert(fo_m2_unpack(r,&f));assert(f.length==1505 && !memcmp(f.payload+1,p,1504));
    memcpy(saved,b,sizeof(b));uint32_t accepted=s.accepted;
    for(unsigned i=0;i<FO_M2_BLOCK;i++) {
        b[i]^=1;assert(!fo_m2_request(&s,b,r));assert(s.accepted==accepted);b[i]^=1;
    }
    assert(!memcmp(saved,b,sizeof(b)));
    /* A same-sequence DIFFERENT request must never masquerade as a replay. */
    p[4]^=1;assert(fo_m2_pack(b,FO_M2_ECHO,1,p,1504));assert(fo_m2_request(&s,b,r));
    assert(r[8]==FO_M2_SEQUENCE && s.accepted==accepted);
    fo_m2_put32(p,session+1);assert(fo_m2_pack(b,FO_M2_ECHO,2,p,4));
    assert(fo_m2_request(&s,b,r)&&r[8]==FO_M2_SESSION && s.next==2);
    fo_m2_put32(p,session);assert(fo_m2_pack(b,77,2,p,4));
    assert(fo_m2_request(&s,b,r)&&r[8]==FO_M2_UNSUPPORTED && s.next==3);
    s.next=65535;assert(fo_m2_pack(b,FO_M2_ECHO,65535,p,4));
    assert(fo_m2_request(&s,b,r)&&s.next==0);
    assert(fo_m2_pack(b,FO_M2_RESET,0,p,4));assert(fo_m2_request(&s,b,r)&&!s.active);
    assert(fo_m2_request(&s,b,r)); /* RESET replay remains idempotent. */
    assert(fo_m2_pack(b,FO_M2_ECHO,1,p,4));assert(!fo_m2_request(&s,b,r));
    session=hello();assert(session==2);fo_m2_abort(&s);assert(!s.active&&!s.cached);
    assert(!fo_m2_pack(b,1,0,p,1519));assert(fo_m2_pack(b,1,0,p,1518));assert(fo_m2_unpack(b,&f));
    b[4]=255;assert(!fo_m2_unpack(b,&f));
}
/* Model the observable wire contract, including persistent acknowledgements,
   ownership, stale BUSY, timer wrap, cancellation and an absent adapter. */
struct mock { bool input,sel,absent,stall,cancel;uint8_t pins;unsigned written,read;uint32_t tick; };
static uint8_t status(void *v) { return ((struct mock *)v)->pins; }
static void in(void *v) { ((struct mock *)v)->input=true; }
static void out(void *v) { struct mock *m=v;assert(!m->sel && m->pins==0);m->input=false; }
static void sel(void *v,bool high) {
    struct mock *m=v;assert(m->input);m->sel=high;
    if(m->absent) return;
    if(high) { m->pins=2;if(m->written==FO_M2_BLOCK) assert(fo_m2_request(&s,b,r)); }
    else { m->pins=0;m->written=m->read=0; }
}
static void wr(void *v,uint8_t n) {
    struct mock *m=v;assert(!m->sel&&!m->input&&m->written<FO_M2_BLOCK);
    b[m->written++]=n;if(!m->stall)m->pins^=1;
}
static uint8_t rd(void *v) {
    struct mock *m=v;assert(m->sel&&m->input&&m->read<FO_M2_BLOCK);
    uint8_t n=r[m->read++];if(!m->stall)m->pins^=1;return n;
}
static uint32_t tick(void *v) { struct mock *m=v;return m->tick++ & 0xffffffu; }
static bool cancel(void *v) { return ((struct mock *)v)->cancel; }
static void host(void) {
    struct mock m={.input=true,.tick=0xfffff0};
    struct fo_m2_io io={&m,status,in,out,sel,wr,rd,tick,cancel};
    fo_m2_init(&s,1);assert(fo_m2_host_sync(&io)==0);
    memset(p,0,8);uint8_t req[FO_M2_BLOCK],reply[FO_M2_BLOCK];
    assert(fo_m2_pack(req,1,0,p,8));assert(fo_m2_host_exchange(&io,req,reply)==0);
    assert(!memcmp(reply,r,sizeof(r))&&m.input&&!m.sel);
    m.stall=true;assert(fo_m2_host_exchange(&io,req,reply)==FO_HOST_TIMEOUT);assert(m.input&&m.sel);
    m.cancel=true;assert(fo_m2_host_sync(&io)==FO_HOST_CANCEL);assert(m.input);
    m.cancel=false;m.absent=true;m.pins=3;m.tick=0xfffff0;
    assert(fo_m2_host_sync(&io)==FO_HOST_TIMEOUT);assert(m.input);
}
int main(void) { protocol();host();puts("M2 protocol/host tests OK"); }
