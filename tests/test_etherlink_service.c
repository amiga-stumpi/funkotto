/* Production FOE1 stream/backend/config + actual WLAN service, fake SDK only. */
#define main legacy_service_main
#include "test_wifi_service.c"
#undef main
#include "funkotto/etherlink_usb.h"
#include "funkotto/raw_wire.h"
static struct fo_el_server server;
static struct fo_el_stream stream;
static struct fo_config config;
static uint8_t request[FO_EL_PACKET], encoded[FO_EL_ENCODED], response[FO_EL_ENCODED];
static uint8_t decoded[FO_EL_PACKET], packet[60];
static struct fo_el_frame reply;
static uint64_t session;
static uint32_t sequence, epoch;
static size_t last_size;
static size_t transfer(void) {
    size_t count=fo_cobs_encode(request,last_size,encoded,sizeof(encoded)); assert(count);
    assert(!fo_el_stream_feed(&stream,&server,0,response));
    for(size_t i=0;i<count;++i) assert(!fo_el_stream_feed(&stream,&server,encoded[i],response));
    size_t n=fo_el_stream_feed(&stream,&server,0,response); assert(n);
    n=fo_cobs_decode(response,n,decoded,sizeof(decoded)); assert(fo_el_unpack(decoded,n,&reply));
    return n;
}
static void rpc(uint8_t op, const uint8_t *p, size_t n) {
    last_size=fo_el_pack(request,sizeof(request),op,session,++sequence,op==EL_HELLO ? 0 : epoch,p,n);
    assert(last_size); transfer(); assert(reply.op==(op|128u) && reply.sequence==sequence);
    epoch=reply.epoch;
}
static void hello(void) {
    uint8_t nonce[8]={1,2,3,4,5,6,7,8}; session=0; sequence=0;
    rpc(EL_HELLO,nonce,sizeof(nonce)); assert(reply.payload[0]==EL_OK);
    session=reply.session; assert(session && ethernet.status.enabled);
}
static void connect_link(void) {
    assert(fo_wifi_submit(REQ_DISCONNECT,NULL));service_step();
    struct fo_profile p={.ssid={'T'},.ssid_len=1,.key={'1','2','3','4','5','6','7','8'},.key_len=8};
    assert(fo_wifi_submit(REQ_SET,&p));service_step();
    assert(fo_wifi_submit(REQ_CONNECT,NULL));service_step();link_event=true;service_step();
    assert(model.state==FO_LINK_UP);
}
static void link_race(void) { fo_eth_link(&ethernet,true,ethernet.status.epoch+1,status.mac); }
int main(void) {
    assert(legacy_service_main()==0); connect_link();
    struct fo_el_backend io; fo_config_init(&config); fo_el_wifi_backend(&io,&config);
    fo_el_init(&server,&io,12345); hello();
    memcpy(packet+6,status.mac,6);packet[12]=8;
    uint32_t tickets[8]; struct fo_el_status snap;
    uint32_t initial=ethernet.status.counters.tx_queued;
    for(unsigned i=0;i<8;++i) {
        rpc(EL_TX,packet,sizeof(packet)); assert(reply.payload[0]==EL_OK);
        tickets[i]=fo_el_u32(reply.payload+1); transfer(); assert(fo_el_u32(reply.payload+1)==tickets[i]);
    }
    assert(ethernet.status.counters.tx_queued==initial+8);
    rpc(EL_TX,packet,sizeof(packet));assert(reply.payload[0]==EL_BUSY);
    fo_net_el_snapshot(&snap);assert(snap.tx_free==0 && snap.tx_done==0);
    for(unsigned i=0;i<8;++i) service_step();
    fo_net_el_snapshot(&snap);assert(snap.tx_free==0 && snap.tx_done==8 && snap.rx_count==8);
    uint32_t rx_full=ethernet.status.counters.rx_full;
    cyw43_cb_process_ethernet(NULL,0,sizeof(packet),packet);assert(ethernet.status.counters.rx_full==rx_full+1);
    for(unsigned i=0;i<8;++i) {
        uint8_t id[4];fo_el_put32(id,tickets[i]);rpc(EL_TX_RESULT,id,4);assert(reply.payload[0]==EL_OK);
        rpc(EL_RX,NULL,0);assert(reply.payload[0]==EL_OK && reply.length==61);
        uint8_t remaining=ethernet.status.rx_used;transfer();assert(ethernet.status.rx_used==remaining);
    }
    fo_net_el_snapshot(&snap);assert(snap.tx_free==8 && snap.tx_done==0 && snap.rx_count==0);
    /* Force another core's epoch transition just before acquisition of the lock.
     * Neither a stale TX nor RX is permitted to mutate the new epoch's queues. */
    uint32_t id=0; uint16_t length=0;
    lock_hook=link_race;assert(fo_net_el_submit(snap.epoch,packet,60,&id)==EL_EPOCH && !id);
    fo_net_el_snapshot(&snap);lock_hook=link_race;
    assert(fo_net_el_pop(snap.epoch,decoded,&length)==EL_EPOCH && !length);
    ethernet_link(true);rpc(EL_STATUS,NULL,0);
    /* Persisting a profile aborts tickets but preserves the active FOE1 owner. */
    rpc(EL_TX,packet,60);assert(reply.payload[0]==EL_OK);id=fo_el_u32(reply.payload+1);
    uint8_t save[]={CFG_SAVE};rpc(EL_CONFIG,save,1);
    assert(reply.payload[0]==EL_OK && reply.payload[1]==CFG_QUEUED);
    uint32_t job=fo_el_u32(reply.payload+2);uint32_t done=shared.completed;
    transfer();assert(config.job==job);service_step();
    assert(shared.completed==done+1 && ethernet.status.enabled && model.wanted);
    uint8_t bytes[5]={CFG_JOB};fo_el_put32(bytes+1,job);rpc(EL_CONFIG,bytes,5);
    assert(reply.payload[0]==EL_OK && reply.payload[1]==CFG_OK && reply.payload[7]==0 && reply.payload[8]==REPLY_OK);
    uint8_t ticket[4];fo_el_put32(ticket,id);rpc(EL_TX_RESULT,ticket,4);assert(reply.payload[0]==EL_ABORTED);
    link_event=true;service_step();rpc(EL_STATUS,NULL,0);assert(reply.payload[1]==1);
    rpc(EL_TX,packet,60);assert(reply.payload[0]==EL_OK);service_step();
    /* In-flight config survives transport loss; no duplicate backend command. */
    uint8_t disconnect[]={CFG_DISCONNECT};rpc(EL_CONFIG,disconnect,1);job=fo_el_u32(reply.payload+2);
    assert(config.pending);fo_el_reset(&server);fo_el_stream_clear(&stream);service_step();hello();
    fo_el_put32(bytes+1,job);rpc(EL_CONFIG,bytes,5);
    assert(reply.payload[1]==CFG_OK && reply.payload[7]==0 && reply.payload[8]==REPLY_OK);
    /* Oversized, corrupt, partial and stale stream input cannot enqueue jobs. */
    for(size_t i=0;i<FO_EL_ENCODED+30u;++i) assert(!fo_el_stream_feed(&stream,&server,7,response));
    assert(!fo_el_stream_feed(&stream,&server,0,response));
    assert(!fo_el_stream_feed(&stream,&server,4,response));fo_el_stream_expire(&stream);
    assert(!fo_el_stream_feed(&stream,&server,8,response));assert(!fo_el_stream_feed(&stream,&server,0,response));
    rpc(EL_STATUS,NULL,0);assert(reply.payload[0]==EL_OK);
    request[last_size-1]^=1;
    size_t count=fo_cobs_encode(request,last_size,encoded,sizeof(encoded));
    for(size_t i=0;i<count;++i) assert(!fo_el_stream_feed(&stream,&server,encoded[i],response));
    assert(!fo_el_stream_feed(&stream,&server,0,response));assert(stream.bad_frames==3);
    for(size_t i=0;i<sizeof(stream.decoded);++i) assert(!stream.decoded[i]);
    rpc(EL_STATUS,NULL,0);assert(reply.payload[0]==EL_OK);
    uint64_t old_session=session;rpc(EL_CLOSE,NULL,0);assert(!ethernet.status.enabled);transfer();
    hello();assert(session!=old_session);fo_el_reset(&server);assert(!ethernet.status.enabled);
    puts("PASS: M5 production stream/service, async queues/replay, epoch races, flash owner, config job recovery, framing and CLOSE");
    return 0;
}
