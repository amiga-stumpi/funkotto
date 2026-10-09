#include "m2.h"
#include "m2_host.h"
#include "os.h"
#include <string.h>
#ifndef FO_DIAG_SOURCE
#define FO_DIAG_SOURCE "development"
#endif
#define REG(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define PRB REG(0xbfe101u)
#define DDRB REG(0xbfe301u)
#define PRA REG(0xbfd000u)
#define DDRA REG(0xbfd200u)
void *SysBase,*DOSBase,*MiscBase;
static uint32_t output;
static bool owned_port,owned_bits,touched,closing;
static uint8_t saved_ddrb,saved_data,saved_ddra,saved_pra;
static uint8_t request[FO_M2_BLOCK],reply[FO_M2_BLOCK],payload[FO_M2_PAYLOAD];
static struct fo_m2_server local;
static struct fo_m2_io io;
static void text(const char *s) { uint32_t n=0;while(s[n]) ++n;(void)os_write(output,s,n); }
static void hex(uint32_t n) {
    char b[9];for(unsigned i=0;i<8;i++) b[i]="0123456789abcdef"[(n>>(28u-4u*i))&15u];
    b[8]=0;text(b);
}
static uint8_t status(void *c) { (void)c;return PRA&3u; }
static void input(void *c) { (void)c;DDRB=0; }
static void direction_out(void *c) { (void)c;DDRB=255; }
static void select_phase(void *c,bool high) {
    (void)c;os_disable();
    /* Do not restore a saved full PRA byte: upper bits belong to serial I/O. */
    uint8_t v=PRA;PRA=(uint8_t)((v&0xfbu)|(high?4u:0u));os_enable();
}
static void write_byte(void *c,uint8_t b) { (void)c;PRB=b; }
static uint8_t read_byte(void *c) { (void)c;return PRB; }
static uint32_t ticks(void *c) {
    (void)c;os_disable(); /* TOD high latches, low unlatches; no timer writes. */
    uint32_t n=(uint32_t)REG(0xbfea01u)<<16;
    n|=(uint32_t)REG(0xbfe901u)<<8;n|=REG(0xbfe801u);os_enable();return n;
}
static bool cancelled(void *c) { (void)c;return !closing && (os_signals()&4096u)!=0; }
static bool acquire(void) {
    MiscBase=os_open_resource("misc.resource");if(!MiscBase) return false;
    if(os_alloc_misc(2,"FunkOttoDiag")) return false;
    owned_port=true;
    if(os_alloc_misc(3,"FunkOttoDiag")) return false;
    owned_bits=true;
    os_disable();
    /* Timer outputs can override DDRB. Refuse, never alter running timers.
       CIAB serial-output mode would drive BUSY/SP and is also incompatible. */
    if((REG(0xbfee01u)&2u)||(REG(0xbfef01u)&2u)||(REG(0xbfde00u)&64u)) {
        os_enable();return false;
    }
    saved_ddrb=DDRB;saved_data=PRB;saved_ddra=DDRA&7u;saved_pra=PRA&7u;
    DDRB=0;PRA=(uint8_t)(PRA|4u);DDRA=(uint8_t)((DDRA&0xf8u)|4u);
    touched=true;os_enable();return true;
}
static void release(void) {
    if(touched) {
        closing=true;
        bool safe=fo_m2_host_sync(&io)==0;
        os_disable();DDRB=0;
        if(safe) {
            /* Pico acknowledged receive direction. Restoring PRB generates one
               STROBE, but cannot make a valid HELLO or enable Pico outputs. */
            PRB=saved_data;DDRB=saved_ddrb;
            PRA=(uint8_t)((PRA&0xf8u)|saved_pra);
            DDRA=(uint8_t)((DDRA&0xf8u)|saved_ddra);
        } else {
            /* No proof of turnaround: leave all data/status bits as inputs.
               Restoring an output direction here could cause a bus collision. */
            DDRA=(uint8_t)(DDRA&0xf8u);
        }
        os_enable();
        if(!safe) text("Port release: no turnaround confirmation; data/status left as inputs.\n");
    }
    if(owned_bits) os_free_misc(3);
    if(owned_port) os_free_misc(2);
}
static bool transact(bool hardware,uint8_t op,uint16_t seq,uint16_t n) {
    if(!fo_m2_pack(request,op,seq,payload,n)) return false;
    if(hardware) {
        int e=fo_m2_host_exchange(&io,request,reply);
        if(e) { text(e==FO_HOST_CANCEL?"Ctrl-C\n":"Handshake timeout\n");return false; }
    } else if(!fo_m2_request(&local,request,reply)) return false;
    struct fo_m2_frame f;
    return fo_m2_unpack(reply,&f) && f.op==(uint8_t)(op|128u) && f.sequence==seq &&
           f.length>=1 && f.payload[0]==FO_M2_OK;
}
static bool run(bool hardware,uint32_t count) {
    if(fo_m2_crc((const uint8_t *)"123456789",9)!=0x29b1u) return false;
    uint32_t nonce=hardware?ticks(NULL):0x12345678u;
    fo_m2_put32(payload,nonce);fo_m2_put32(payload+4,~nonce);
    if(!transact(hardware,FO_M2_HELLO,0,8)) return false;
    struct fo_m2_frame f;
    if(!fo_m2_unpack(reply,&f)||f.length!=18||memcmp(f.payload+1,payload,8)||
       f.payload[13]!=1 || f.payload[14]!=5 || f.payload[15]!=220 ||
       f.payload[16]!=6 || f.payload[17]!=0) return false;
    uint32_t session=fo_m2_u32(f.payload+9);
    text("HELLO session=");hex(session);text("\n");
    static const uint16_t sizes[]={0,1,2,7,63,255,512,1500};
    uint16_t seq=1;
    for(uint32_t i=0;i<count;i++,seq=(uint16_t)(seq+1u)) {
        uint16_t length=sizes[i&7u];fo_m2_put32(payload,session);
        uint32_t random=i+1u;
        for(unsigned j=0;j<length;j++) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            unsigned pattern=(unsigned)((i>>3)&7u);
            payload[4+j]=pattern==0?0:pattern==1?255:pattern==2?0x55:pattern==3?0xaa:
                pattern==4?(uint8_t)(1u<<(j&7u)):pattern==5?(uint8_t)~(1u<<(j&7u)):(uint8_t)random;
        }
        if(!transact(hardware,FO_M2_ECHO,seq,(uint16_t)(4u+length)) ||
           !fo_m2_unpack(reply,&f) || f.length!=5u+length ||
           memcmp(f.payload+1,payload,4u+length)) { text("ECHO failed at ");hex(i);text("\n");return false; }
    }
    text("ECHO verified count=0x");hex(count);text("\n");return true;
}
static bool token(const char **p,const char *end,const char *word) {
    while(*p<end && (**p==' '||**p=='\n'||**p=='\r')) ++*p;
    const char *q=*p;while(q<end && *word && *q==*word) { ++q;++word; }
    if(*word || (q<end && *q!=' '&&*q!='\n'&&*q!='\r')) return false;
    *p=q;return true;
}
int diag_main(const char *args,uint32_t length) {
    DOSBase=os_open_library("dos.library",34);if(!DOSBase) return 20;
    output=os_output();int result=20;
    io=(struct fo_m2_io){NULL,status,input,direction_out,select_phase,write_byte,read_byte,ticks,cancelled};
    text("FunkOttoDiag M2 / 68000 / OS 1.3 / polling diagnostic only\n");
    text("source=" FO_DIAG_SOURCE "\n");
    const char *end=args+length,*p=args;
    bool hardware=token(&p,end,"RUN");uint32_t count=64;
    if(hardware) {
        if(!token(&p,end,"M0-VERIFIED")) goto usage;
        while(p<end && *p==' ') ++p;
        if(p<end && *p>='0'&&*p<='9') {
            count=0;while(p<end && *p>='0'&&*p<='9') {
                if(count>100000u) goto usage;
                count=count*10u+(unsigned)(*p++-'0');
            }
        }
    } else if(!token(&p,end,"SELFTEST")) goto usage;
    while(p<end && (*p==' '||*p=='\n'||*p=='\r')) ++p;
    if(p!=end || count==0 || count>100000u) goto usage;
    if(hardware) {
        text("Laboratory bus test after M0; Ctrl-C aborts.\n");
        if(!acquire()) { text("Parallel resources busy, missing, or timer/serial output conflict.\n");goto done; }
        if(fo_m2_host_sync(&io)) { text("SYNC failed: adapter locked, absent, or timeout.\n");goto done; }
    } else fo_m2_init(&local,0x12340000u);
    result=run(hardware,count)?0:20;
    text(result?"FAIL\n":hardware?"PASS: transport (hardware timing still requires measurements).\n":"PASS: local protocol selftest; no CIA access.\n");
    goto done;
usage:
    text("Usage: FunkOttoDiag SELFTEST\n       FunkOttoDiag RUN M0-VERIFIED [1..100000]\n");
done:
    release();os_close_library(DOSBase);return result;
}
