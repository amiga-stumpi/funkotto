#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "sim.h"
extern int cfg_selftest(void);
static unsigned calls, waits, mode;
static int wait_ok(void) { ++waits; return mode!=2; }
static size_t broken(uint8_t op, const uint8_t *p, size_t n, uint8_t *out) {
    (void)p; (void)n; ++calls;
    if (mode==0) return 0;
    if (mode==1 || mode==2) {
        if (op==CFG_SAVE) { out[0]=CFG_QUEUED; cfg_put32(out+1,1); return 5; }
        out[0]=CFG_OK; cfg_put32(out+1,1); out[5]=CFG_SAVE; out[6]=1; out[7]=255; return 8;
    }
    if (mode==3) { out[0]=CFG_OK; return 1; }
    if (mode==4) return FO_CFG_MAX+1;
    if (mode==5) { out[0]=CFG_OK; out[50]=33; return 84; }
    if (mode==6) { out[0]=CFG_BUSY; out[1]=0; return 2; }
    if (mode==7) { out[0]=CFG_QUEUED; cfg_put32(out+1,0); return 5; }
    out[0]=CFG_OK; cfg_put32(out+1,99); out[5]=CFG_SAVE; out[7]=REPLY_OK; return 8;
}
int main(void) {
    assert(!cfg_selftest());
    struct cfg_client c={{broken,wait_ok},{0},0,0,0};
    mode=0; assert(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_TRANSPORT && calls==1);
    calls=0; waits=0; mode=1;
    assert(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_TIMEOUT && calls==301 && waits==300);
    calls=0; waits=0; mode=2;
    assert(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_CANCELLED && calls==1 && waits==1);
    for (mode=3;mode<=6;++mode) assert(cfg_query(&c,CFG_STATUS,NULL,0)==CC_FORMAT);
    mode=7; assert(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_FORMAT);
    uint8_t job[4]={0,0,0,1}; mode=8;
    assert(cfg_query(&c,CFG_JOB,job,4)==CC_FORMAT);
    uint8_t odd[6]={0,0x12,0x34,0x56,0x78,0};
    assert(cfg_u32(odd+1)==0x12345678 && cfg_u16(odd+1)==0x1234);
    puts("Amiga config: lifecycle, malformed replies, cancellation, timeout, no blind replay: PASS");
    return 0;
}
