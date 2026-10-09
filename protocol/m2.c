#include "m2.h"
#include <string.h>

uint16_t fo_m2_crc(const uint8_t *p, size_t n) {
    uint16_t crc=0xffffu;
    while (n--) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (unsigned i=0;i<8;i++) crc=(uint16_t)(((uint32_t)crc<<1)^((crc&0x8000u)?0x1021u:0u));
    }
    return crc;
}
static uint16_t u16(const volatile uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static void put16(volatile uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
uint32_t fo_m2_u32(const uint8_t *bytes) {
    const volatile uint8_t *p=bytes; /* May be odd-aligned on a 68000. */
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
void fo_m2_put32(uint8_t *bytes,uint32_t n) {
    volatile uint8_t *p=bytes;
    p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;
}
bool fo_m2_pack(uint8_t *b,uint8_t op,uint16_t seq,const uint8_t *p,uint16_t n) {
    if (n>FO_M2_PAYLOAD || (n && !p)) return false;
    memset(b,0,FO_M2_BLOCK); b[0]=0x41;b[1]=0x57;b[2]=FO_M2_VERSION;b[3]=op;
    put16(b+4,n);put16(b+6,seq);
    if(n) memcpy(b+8,p,n);
    put16(b+8+n,fo_m2_crc(b,8u+n));return true;
}
bool fo_m2_unpack(const uint8_t *b,struct fo_m2_frame *f) {
    if(b[0]!=0x41 || b[1]!=0x57 || b[2]!=FO_M2_VERSION) return false;
    uint16_t n=u16(b+4);
    if(n>FO_M2_PAYLOAD || u16(b+8+n)!=fo_m2_crc(b,8u+n)) return false;
    for(size_t i=10u+n;i<FO_M2_BLOCK;i++) if(b[i]) return false;
    f->op=b[3];f->length=n;f->sequence=u16(b+6);f->payload=b+8;return true;
}
void fo_m2_init(struct fo_m2_server *s,uint32_t seed) {
    memset(s,0,sizeof(*s)); s->generation=seed;
}
void fo_m2_abort(struct fo_m2_server *s) { s->active=false;s->cached=false; }
bool fo_m2_request(struct fo_m2_server *s,const uint8_t *b,uint8_t *reply) {
    struct fo_m2_frame f;
    uint8_t p[FO_M2_PAYLOAD];uint16_t n=1;
    if(!fo_m2_unpack(b,&f) || (f.op&0x80u)) { ++s->invalid;return false; }
    if(s->cached && memcmp(b,s->last,FO_M2_BLOCK)==0) {
        memcpy(reply,s->reply,FO_M2_BLOCK);++s->replays;return true;
    }
    p[0]=FO_M2_OK;
    if(f.op==FO_M2_HELLO) {
        if(f.length!=8 || f.sequence!=0) { ++s->invalid;return false; }
        /* Each accepted, non-duplicate HELLO replaces the session. Boot seed is
           random; this token rejects stale transfers, it is not authentication. */
        if(++s->generation==0) ++s->generation;
        s->session=s->generation;s->active=true;
        memcpy(p+1,f.payload,8);fo_m2_put32(p+9,s->session);
        p[13]=FO_M2_VERSION;put16(p+14,FO_M2_ECHO_MAX);put16(p+16,FO_M2_BLOCK);n=18;
    } else {
        if(!s->active) { ++s->invalid;return false; }
        if(f.length<4 || fo_m2_u32(f.payload)!=s->session) p[0]=FO_M2_SESSION;
        else if(f.sequence!=s->next) { p[0]=FO_M2_SEQUENCE;++s->sequence_errors; }
        if(p[0]!=FO_M2_OK) return fo_m2_pack(reply,(uint8_t)(f.op|0x80u),f.sequence,p,1);
        fo_m2_put32(p+1,s->session);n=5;
        switch(f.op) {
        case FO_M2_INFO:
            if(f.length!=4) p[0]=FO_M2_LENGTH;
            else { memcpy(p+5,"FunkOtto M2",11);n=16; }
            break;
        case FO_M2_ECHO:
            if(f.length>4u+FO_M2_ECHO_MAX) p[0]=FO_M2_LENGTH;
            else { memcpy(p+5,f.payload+4,(size_t)f.length-4u);n=(uint16_t)(f.length+1u); }
            break;
        case FO_M2_RESET:
            if(f.length!=4) p[0]=FO_M2_LENGTH;
            else s->active=false;
            break;
        default: p[0]=FO_M2_UNSUPPORTED;break;
        }
    }
    s->next=(uint16_t)(f.sequence+1u);++s->accepted;
    (void)fo_m2_pack(reply,(uint8_t)(f.op|0x80u),f.sequence,p,n);
    memcpy(s->last,b,FO_M2_BLOCK);memcpy(s->reply,reply,FO_M2_BLOCK);s->cached=true;
    return true;
}
