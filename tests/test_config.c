#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "funkotto/config_wire.h"
#include "funkotto/raw_wire.h"
void test_reset(void); void test_complete(unsigned r); unsigned test_calls(void);
unsigned test_net_enables(void); void test_busy(unsigned b);
struct fo_wifi_status *test_status(void);
static struct fo_config_wire wire;
static uint8_t frame[FO_CFG_SIZE], coded[FO_CFG_ENCODED], out[FO_CFG_ENCODED], reply[FO_CFG_SIZE];
static size_t exchange(uint8_t op,uint64_t session,uint32_t seq,const uint8_t *p,size_t n) {
    size_t len=fo_raw_packet(frame,op,session,seq,p,n); frame[0]=2;
    fo_put32(frame+len-4,fo_crc32(frame,len-4));
    len=fo_cobs_encode(frame,len,coded,sizeof(coded)); assert(len);
    (void)fo_config_wire_feed(&wire,0,out);
    for (size_t i=0;i<len;++i) assert(!fo_config_wire_feed(&wire,coded[i],out));
    len=fo_config_wire_feed(&wire,0,out);
    return fo_cobs_decode(out,len,reply,sizeof(reply));
}
static void start(void) {
    test_reset(); fo_config_wire_start(&wire,42);
    assert(exchange(0,0,1,NULL,0)==38); assert(reply[16]==CFG_OK);
    assert(!memcmp(reply+17,"FOC1",4)); assert(fo_get32(reply+12)==42);
}
static void replay(void) {
    start(); assert(exchange(CFG_SAVE,42,2,NULL,0)==25);
    assert(reply[16]==CFG_QUEUED && fo_get32(reply+17)==1 && test_calls()==1);
    assert(exchange(CFG_SAVE,42,2,NULL,0)==25 && test_calls()==1);
    test_complete(REPLY_OK);
    assert(exchange(CFG_SAVE,42,2,NULL,0)==25 && test_calls()==1);
    assert(exchange(CFG_ERASE,42,2,NULL,0)==21 && reply[16]==CFG_SEQUENCE && test_calls()==1);
    uint8_t job[4]; fo_put32(job,1);
    assert(exchange(CFG_JOB,42,3,job,4)==28 && reply[22]==0 && reply[23]==REPLY_OK);
    assert(exchange(CFG_SAVE,42,2,NULL,0)==21 && reply[16]==CFG_SEQUENCE && test_calls()==1);
    assert(exchange(CFG_SAVE,43,4,NULL,0)==21 && reply[16]==CFG_SESSION && test_calls()==1);
    assert(exchange(CFG_SAVE,42,5,NULL,0)==21 && reply[16]==CFG_SEQUENCE && test_calls()==1);
    assert(exchange(CFG_SAVE,42,4,NULL,0)==25 && test_calls()==2);
    assert(test_net_enables()==0);
}
static void jobs(void) {
    start();
    assert(exchange(CFG_SCAN,42,2,NULL,0)==25);
    assert(exchange(CFG_SAVE,42,3,NULL,0)==21 && reply[16]==CFG_BUSY && test_calls()==1);
    uint8_t job[4]; fo_put32(job,1);
    assert(exchange(CFG_JOB,42,4,job,4)==28 && reply[22]==1 && reply[23]==255);
    test_complete(REPLY_BUSY);
    assert(exchange(CFG_JOB,42,5,job,4)==28 && reply[22]==0 && reply[23]==REPLY_BUSY);
    fo_put32(job,99); assert(exchange(CFG_JOB,42,6,job,4)==21 && reply[16]==CFG_STALE);
    test_busy(1); assert(exchange(CFG_SAVE,42,7,NULL,0)==21 && reply[16]==CFG_BUSY);
    test_busy(0); wire.config.job=UINT32_MAX;
    assert(exchange(CFG_SAVE,42,8,NULL,0)==21 && reply[16]==CFG_BUSY);
}
static void profile(void) {
    const uint8_t p[]={1,5,8,'L','i','n','u','x','s','e','c','r','e','t','4','2'};
    start(); assert(exchange(CFG_SET,42,2,p,sizeof(p))==25 && reply[16]==CFG_QUEUED);
    for(size_t i=0;i<sizeof(wire.decoded);++i) assert(wire.decoded[i]==0);
    for(size_t i=0;i<sizeof(wire.input);++i) assert(wire.input[i]==0);
    test_complete(REPLY_OK);
    assert(exchange(CFG_STATUS,42,3,NULL,0)==76); assert(reply[66]==5);
    assert(!memcmp(reply+67,"Linux",5));
    for(size_t i=20;i<sizeof(wire.request);++i) assert(wire.request[i]==0);
    assert(exchange(CFG_SET,42,4,p,sizeof(p)-1)==21 && reply[16]==CFG_INVALID);
    uint8_t bad[sizeof(p)]; memcpy(bad,p,sizeof(p)); bad[8]=0;
    assert(exchange(CFG_SET,42,5,bad,sizeof(bad))==21 && reply[16]==CFG_INVALID);
    bad[0]=2; assert(exchange(CFG_SET,42,6,bad,sizeof(bad))==21 && reply[16]==CFG_INVALID);
    assert(test_calls()==1);
    fo_config_wire_close(&wire);
    const uint8_t *w=(const uint8_t *)&wire; for(size_t i=0;i<sizeof(wire);++i) assert(w[i]==0);
}
static void scan(void) {
    start(); struct fo_wifi_status *s=test_status();
    s->scan_generation=5; s->scan_count=1; s->scanning=true;
    uint8_t p[5]={0,0,0,5,0};
    assert(exchange(CFG_SCAN_GET,42,2,p,5)==21 && reply[16]==CFG_BUSY);
    s->scanning=false; s->results[0].rssi=-88; s->results[0].channel=10;
    assert(exchange(CFG_SCAN_GET,42,3,p,5)==40 && reply[35]==0); /* hidden SSID */
    assert(reply[27]==255 && reply[28]==168);
    p[3]=4; assert(exchange(CFG_SCAN_GET,42,4,p,5)==21 && reply[16]==CFG_STALE);
    p[3]=5; p[4]=1; assert(exchange(CFG_SCAN_GET,42,5,p,5)==21 && reply[16]==CFG_EMPTY);
}
static void malformed(void) {
    start();
    assert(exchange(99,42,2,NULL,0)==21 && reply[16]==CFG_UNSUPPORTED);
    uint8_t p=0; assert(exchange(CFG_SAVE,42,3,&p,1)==21 && reply[16]==CFG_INVALID);
    assert(exchange(CFG_STATUS,42,4,&p,1)==21 && reply[16]==CFG_INVALID);
    for(unsigned i=0;i<1000;++i) (void)fo_config_wire_feed(&wire,1,out);
    assert(!fo_config_wire_feed(&wire,0,out));
    assert(!fo_config_wire_feed(&wire,2,out));
    fo_config_wire_expire_partial(&wire); assert(!fo_config_wire_feed(&wire,0,out));
    assert(exchange(CFG_STATUS,42,5,NULL,0)>20);
    size_t n=fo_raw_packet(frame,CFG_SAVE,42,6,NULL,0); /* W3 version cannot execute */
    n=fo_cobs_encode(frame,n,coded,sizeof(coded));
    for(size_t i=0;i<n;++i) (void)fo_config_wire_feed(&wire,coded[i],out);
    assert(!fo_config_wire_feed(&wire,0,out));
    assert(test_calls()==0);
    uint32_t rng=12345;
    for(unsigned i=0;i<50000;++i) { rng=rng*1664525u+1013904223u; (void)fo_config_wire_feed(&wire,(uint8_t)(rng>>24),out); }
    assert(test_calls()==0);
    fo_config_wire_start(&wire,43);
    assert(exchange(CFG_SAVE,42,6,NULL,0)==21 && reply[16]==CFG_SESSION);
    assert(test_calls()==0);
}
int main(void) { replay(); jobs(); profile(); scan(); malformed(); puts("PASS: config protocol, jobs, replay, scan, secrets and malformed frames"); }
