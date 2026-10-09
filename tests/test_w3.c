#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "funkotto/raw_wire.h"
static struct fo_eth eth;
void fo_net_enable(bool v) { fo_eth_enable(&eth,v); }
void fo_net_snapshot(struct fo_eth_status *s) { *s=eth.status; }
enum fo_net_result fo_net_submit(const uint8_t *p,size_t n,uint32_t *t) { return fo_eth_submit(&eth,p,n,t); }
enum fo_net_result fo_net_result(uint32_t t,int32_t *e) { return fo_eth_result(&eth,t,e); }
enum fo_net_result fo_net_pop(struct fo_frame *p) { return fo_eth_pop(&eth,p); }
static uint8_t mac[6]={2,1,2,3,4,5};
static uint8_t frame[FO_ETH_MAX], encoded_request[FO_RAW_ENCODED], output[FO_RAW_ENCODED], decoded[FO_RAW_SIZE];
static struct fo_raw raw;
static void init(void) {
    fo_eth_init(&eth); fo_eth_link(&eth,true,1,mac); fo_eth_enable(&eth,true);
    memset(frame,0xa5,sizeof(frame)); memcpy(frame+6,mac,6); frame[12]=8; frame[13]=0;
}
static void queues(void) {
    init(); uint32_t tickets[FO_ETH_SLOTS], ticket; int32_t sdk=99; struct fo_frame copy;
    assert(fo_eth_submit(&eth,NULL,14,&ticket)==NET_INVALID);
    assert(fo_eth_submit(&eth,frame,13,&ticket)==NET_INVALID);
    assert(fo_eth_submit(&eth,frame,1515,&ticket)==NET_INVALID);
    frame[6]^=1; assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_INVALID); frame[6]^=1;
    frame[12]=0x81; frame[13]=0; assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_INVALID); frame[12]=8;
    for (unsigned i=0;i<FO_ETH_SLOTS;++i) assert(fo_eth_submit(&eth,frame,i==0?14:FO_ETH_MAX,&tickets[i])==NET_OK);
    assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_BUSY);
    frame[14]=0x12; assert(eth.tx[1].frame.data[14]==0xa5);
    assert(fo_eth_claim(&eth,&copy,&ticket) && ticket==tickets[0] && copy.len==60);
    for (unsigned i=14;i<60;++i) assert(!copy.data[i]);
    assert(fo_eth_result(&eth,ticket,&sdk)==NET_PENDING);
    fo_eth_complete(&eth,ticket,-8); assert(fo_eth_result(&eth,ticket,&sdk)==NET_DRIVER && sdk==-8);
    for (unsigned i=1;i<FO_ETH_SLOTS;++i) {
        assert(fo_eth_claim(&eth,&copy,&ticket) && ticket==tickets[i] && copy.len==1514);
        fo_eth_complete(&eth,ticket,0); assert(fo_eth_result(&eth,ticket,&sdk)==NET_OK && sdk==0);
    }
    assert(!eth.status.tx_used && eth.status.counters.tx_ok==7 && eth.status.counters.tx_error==1);
    for (unsigned round=0;round<20;++round) {
        for (unsigned i=0;i<FO_ETH_SLOTS;++i) { frame[14]=(uint8_t)i; fo_eth_receive(&eth,frame,1514); }
        fo_eth_receive(&eth,frame,1514); assert(eth.status.rx_used==8);
        memset(frame+14,0x77,sizeof(frame)-14);
        for (unsigned i=0;i<FO_ETH_SLOTS;++i) assert(fo_eth_pop(&eth,&copy)==NET_OK && copy.len==1514 && copy.data[14]==i);
        assert(fo_eth_pop(&eth,&copy)==NET_EMPTY);
    }
    assert(eth.status.counters.rx_full==20 && eth.status.counters.rx_high_water==8);
    assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_OK); assert(fo_eth_claim(&eth,&copy,&ticket));
    fo_eth_receive(&eth,frame,60); fo_eth_link(&eth,false,2,mac);
    fo_eth_complete(&eth,ticket,0); assert(fo_eth_result(&eth,ticket,&sdk)==NET_ABORTED);
    assert(!eth.status.rx_used && eth.status.counters.rx_flushed==1);
    assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_NO_LINK);
    fo_eth_link(&eth,true,3,mac);
    assert(fo_eth_submit(&eth,frame,60,&ticket)==NET_OK); assert(fo_eth_claim(&eth,&copy,&ticket));
    fo_eth_enable(&eth,false); fo_eth_enable(&eth,true);
    uint32_t new_ticket; assert(fo_eth_submit(&eth,frame,60,&new_ticket)==NET_OK && ticket!=new_ticket);
    fo_eth_complete(&eth,ticket,0); assert(fo_eth_result(&eth,new_ticket,&sdk)==NET_PENDING);
    fo_eth_link(&eth,true,4,mac); assert(fo_eth_result(&eth,new_ticket,&sdk)==NET_ABORTED);
}
static size_t send_request(uint8_t type,uint64_t session,uint32_t seq,const uint8_t *p,size_t n,bool corrupt) {
    uint8_t request[FO_RAW_SIZE]; size_t count=fo_raw_packet(request,type,session,seq,p,n);
    if (corrupt) request[count-1]^=1;
    size_t size=fo_cobs_encode(request,count,encoded_request,sizeof(encoded_request));
    assert(!fo_raw_feed(&raw,0,output));
    for (size_t i=0;i<size;++i) assert(!fo_raw_feed(&raw,encoded_request[i],output));
    size=fo_raw_feed(&raw,0,output);
    if (size) { size_t d=fo_cobs_decode(output,size,decoded,sizeof(decoded)); assert(d>=21 && fo_crc32(decoded,d-4)==fo_get32(decoded+d-4)); }
    return size;
}
static void wire(void) {
    init(); fo_raw_start(&raw,0x123456789abcdef0ULL);
    assert(send_request(RAW_HELLO,0,1,NULL,0,false)); assert(decoded[16]==NET_OK && !memcmp(decoded+17,mac,6));
    assert(!send_request(RAW_TX,raw.session,2,frame,1514,true)); assert(!eth.status.counters.tx_queued);
    assert(!send_request(RAW_TX,raw.session,2,frame,1514,false)); assert(raw.pending && eth.status.counters.tx_queued==1);
    assert(!send_request(RAW_TX,raw.session,2,frame,1514,false)); assert(eth.status.counters.tx_queued==1);
    frame[14]^=1; assert(send_request(RAW_TX,raw.session,2,frame,1514,false)); assert(decoded[16]==NET_SEQUENCE); frame[14]^=1;
    assert(send_request(RAW_STATS,raw.session,3,NULL,0,false)); assert(decoded[16]==NET_SEQUENCE);
    struct fo_frame tx; uint32_t ticket; assert(fo_eth_claim(&eth,&tx,&ticket)); fo_eth_complete(&eth,ticket,0);
    size_t n=fo_raw_poll(&raw,output); assert(n && !raw.pending);
    uint8_t first[FO_RAW_ENCODED]; memcpy(first,output,n);
    assert(send_request(RAW_TX,raw.session,2,frame,1514,false)==n && !memcmp(first,output,n));
    assert(eth.status.counters.tx_queued==1 && eth.status.counters.tx_ok==1);
    fo_eth_receive(&eth,frame,1514); fo_eth_receive(&eth,frame,60);
    assert(send_request(RAW_RX,raw.session,3,NULL,0,false)); assert(decoded[16]==NET_OK && eth.status.rx_used==1);
    assert(!memcmp(decoded+17,frame,1514));
    assert(send_request(RAW_RX,raw.session,3,NULL,0,false)); assert(eth.status.rx_used==1);
    assert(send_request(RAW_RX,raw.session+1,4,NULL,0,false)); assert(decoded[16]==NET_SESSION && eth.status.rx_used==1);
    assert(send_request(RAW_RX,raw.session,4,NULL,0,false)); assert(eth.status.rx_used==0);
    assert(send_request(RAW_RX,raw.session,5,NULL,0,false)); assert(decoded[16]==NET_EMPTY);
    assert(send_request(RAW_STATS,raw.session,6,NULL,0,false)); assert(decoded[16]==NET_OK);
    for (unsigned i=0;i<10000;++i) assert(!fo_raw_feed(&raw,1,output));
    assert(!fo_raw_feed(&raw,0,output));
    assert(send_request(RAW_STATS,raw.session,7,NULL,0,false)); assert(raw.bad_frames==2);
    for (size_t size=0;size<=FO_RAW_SIZE;++size) {
        uint8_t input[FO_RAW_SIZE], result[FO_RAW_SIZE];
        for (size_t i=0;i<size;++i) input[i]=(uint8_t)(i*73u+size);
        n=fo_cobs_encode(input,size,output,sizeof(output)); assert(n);
        assert(fo_cobs_decode(output,n,result,sizeof(result))==size && !memcmp(input,result,size));
        if (size) assert(!fo_cobs_decode(output,n,result,size-1));
    }
    assert(fo_crc32((const uint8_t *)"123456789",9)==0xcbf43926u);
    /* Malformed random input must remain bounded and resynchronize. */
    uint32_t random=1;
    for (unsigned i=0;i<200000;++i) { random=random*1664525u+1013904223u; (void)fo_raw_feed(&raw,(uint8_t)(random>>24),output); }
    (void)fo_raw_feed(&raw,0,output);
    assert(send_request(RAW_STATS,raw.session,8,NULL,0,false));
}
int main(void) { queues(); wire(); puts("PASS: W3 buffer ownership, bounds, overflow, epochs, padding, CRC/COBS, replay and malformed stream recovery"); }
