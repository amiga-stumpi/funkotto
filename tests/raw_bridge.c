/* ctypes harness: the real C protocol and queues, with deterministic SDK TX. */
#include <string.h>
#include "funkotto/raw_wire.h"
static struct fo_eth eth;
static struct fo_raw raw;
void fo_net_enable(bool v) { fo_eth_enable(&eth,v); }
void fo_net_snapshot(struct fo_eth_status *s) { *s=eth.status; }
enum fo_net_result fo_net_submit(const uint8_t *p,size_t n,uint32_t *t) { return fo_eth_submit(&eth,p,n,t); }
enum fo_net_result fo_net_result(uint32_t t,int32_t *e) { return fo_eth_result(&eth,t,e); }
enum fo_net_result fo_net_pop(struct fo_frame *p) { return fo_eth_pop(&eth,p); }
void bridge_start(void) {
    uint8_t mac[6]={2,1,2,3,4,5}; fo_eth_init(&eth); fo_eth_link(&eth,true,7,mac);
    fo_raw_start(&raw,0x123456789abcdef0ULL);
}
size_t bridge_feed(uint8_t b,uint8_t *out) { return fo_raw_feed(&raw,b,out); }
size_t bridge_poll(uint8_t *out) {
    struct fo_frame frame; uint32_t ticket;
    if (fo_eth_claim(&eth,&frame,&ticket)) fo_eth_complete(&eth,ticket,0);
    return fo_raw_poll(&raw,out);
}
void bridge_rx(const uint8_t *p,size_t n) { fo_eth_receive(&eth,p,n); }
