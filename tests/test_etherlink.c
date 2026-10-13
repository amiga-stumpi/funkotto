#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "etherlink.h"
#include "funkotto/ethernet.h"
static struct fo_eth eth;
static struct fo_el_server server;
static struct fo_frame workspace;
static uint8_t mac[6]={2,1,2,3,4,5};
static uint8_t frame[FO_EL_MTU], request[FO_EL_PACKET], reply[FO_EL_PACKET], saved[FO_EL_PACKET];
static struct fo_el_frame response;
static uint32_t seq, epoch, config_serial;
static uint64_t session;
static unsigned enables, disables, submits, pops, configs, race;
static void enable(void *ctx, bool value) {
    assert(ctx==&eth); if(value) ++enables; else ++disables; fo_eth_enable(&eth,value);
}
static void snapshot(void *ctx, struct fo_el_status *out) {
    assert(ctx==&eth); memset(out,0,sizeof(*out));
    const struct fo_eth_status *s=&eth.status;
    memcpy(out->mac,s->mac,6); out->link=s->link; out->epoch=s->epoch;
    out->tx_free=(uint8_t)(FO_ETH_SLOTS-s->tx_used); out->rx_count=s->rx_used;
    for(unsigned i=0;i<FO_ETH_SLOTS;++i) if(eth.tx[i].state==TX_DONE) ++out->tx_done;
    out->rx_serial=s->counters.rx_queued+s->counters.rx_full;
    out->tx_serial=s->counters.tx_ok+s->counters.tx_error+s->counters.tx_aborted;
    out->config_serial=config_serial;
}
static uint8_t tx(void *ctx, uint32_t expected, const uint8_t *p, size_t n, uint32_t *ticket) {
    assert(ctx==&eth); ++submits;
    if(race==1) { ++epoch; fo_eth_link(&eth,true,epoch,mac); race=0; }
    if(expected!=eth.status.epoch) return EL_EPOCH;
    return (uint8_t)fo_eth_submit(&eth,p,n,ticket);
}
static uint8_t rx(void *ctx, uint32_t expected, uint8_t *out, uint16_t *length) {
    assert(ctx==&eth); ++pops;
    if(race==2) {
        ++epoch; fo_eth_link(&eth,true,epoch,mac); fo_eth_receive(&eth,frame,60); race=0;
    }
    if(expected!=eth.status.epoch) return EL_EPOCH;
    uint8_t result=(uint8_t)fo_eth_pop(&eth,&workspace);
    if(!result) { *length=workspace.len; memcpy(out,workspace.data,*length); }
    return result;
}
static uint8_t done(void *ctx, uint32_t ticket, int32_t *sdk) {
    assert(ctx==&eth); return (uint8_t)fo_eth_result(&eth,ticket,sdk);
}
static size_t config(void *ctx, uint8_t op, const uint8_t *p, size_t n, uint8_t *out) {
    assert(ctx==&eth); ++configs;
    assert(op==5 && n==12 && p[0]==1 && p[1]==1 && p[2]==8);
    assert(!memcmp(p+3,"Xtest1234",9)); /* key stays in request only */
    out[0]=1; fo_el_put32(out+1,42); return 5; /* FOC1 QUEUED */
}
static struct fo_el_backend backend={&eth,enable,snapshot,tx,done,rx,config};
static void start(void) {
    fo_eth_init(&eth); epoch=7; fo_eth_link(&eth,true,epoch,mac);
    enables=disables=submits=pops=configs=race=0; config_serial=0;
    memset(frame,0x5a,sizeof(frame)); memcpy(frame+6,mac,6); frame[12]=8; frame[13]=0;
    fo_el_init(&server,&backend,UINT64_C(0x1234567800000000));
    session=0; seq=0;
}
static size_t send(uint8_t op, const uint8_t *p, size_t n) {
    size_t length=fo_el_pack(request,sizeof(request),op,session,++seq,
                            op==EL_HELLO ? 0 : epoch,p,n);
    assert(length); size_t result=fo_el_request(&server,request,length,reply);
    assert(result && fo_el_unpack(reply,result,&response));
    assert(response.op==(op|128u) && response.sequence==seq);
    return result;
}
static void hello(void) {
    static const uint8_t nonce[8]={1,3,5,7,9,11,13,15};
    send(EL_HELLO,nonce,8); assert(response.payload[0]==EL_OK && response.length==25);
    assert(!memcmp(response.payload+1,nonce,8)); session=response.session;
    assert(session && response.epoch==epoch && enables==1);
    assert(fo_el_u16(response.payload+13)==1514 && response.payload[15]==8);
}
static void retry(size_t length) {
    memcpy(saved,reply,length); unsigned tx_calls=submits, rx_calls=pops, cfg_calls=configs;
    size_t n=fo_el_request(&server,request,28u+fo_el_u16(request+6),reply);
    assert(n==length && !memcmp(saved,reply,n));
    assert(submits==tx_calls && pops==rx_calls && configs==cfg_calls);
    assert(fo_el_unpack(reply,n,&response));
}
static void codec(void) {
    assert(fo_el_crc32((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    uint8_t odd[FO_EL_PACKET+2];
    for(size_t n=0;n<=FO_EL_PAYLOAD;++n) {
#ifdef FO_EL_M68K_TEST
        if(n!=0 && n!=1 && n!=14 && n!=60 && n!=1514 && n!=1515) continue;
#endif
        memset(saved,0xa5,n);
        size_t size=fo_el_pack(odd+1,sizeof(odd)-1,EL_TX,UINT64_C(0x123456789abcdef0),12,19,saved,n);
        struct fo_el_frame f; assert(size==28+n && fo_el_unpack(odd+1,size,&f));
        assert(f.length==n && f.sequence==12 && f.epoch==19 && f.session==UINT64_C(0x123456789abcdef0));
        assert(!memcmp(f.payload,saved,n));
        assert(!fo_el_unpack(odd+1,size-1,&f));
        odd[size]^=1; assert(!fo_el_unpack(odd+1,size,&f));
    }
    assert(!fo_el_pack(odd,27,EL_TX,1,1,1,NULL,0));
    assert(!fo_el_pack(odd,sizeof(odd),EL_TX,1,1,1,NULL,1));
    assert(!fo_el_pack(odd,sizeof(odd),EL_TX,1,1,1,saved,FO_EL_PAYLOAD+1));
    puts("FOE1 codec: CRC vector, all lengths, odd addresses and truncation PASS");
}
static void queues(void) {
    start(); hello(); uint32_t tickets[8];
    size_t n=send(EL_STATUS,NULL,0); assert(n==57 && response.payload[2]==8);
    for(unsigned i=0;i<8;++i) {
        n=send(EL_TX,frame,i==0 ? 14 : 1514); assert(response.payload[0]==EL_OK);
        tickets[i]=fo_el_u32(response.payload+1); retry(n);
    }
    assert(eth.status.tx_used==8 && eth.status.counters.tx_queued==8);
    send(EL_TX,frame,60); assert(response.payload[0]==EL_BUSY);
    uint8_t ticket[4]; fo_el_put32(ticket,tickets[0]);
    n=send(EL_TX_RESULT,ticket,4); assert(response.payload[0]==EL_PENDING); retry(n);
    uint32_t id; assert(fo_eth_claim(&eth,&workspace,&id) && id==tickets[0]);
    assert(workspace.len==60); for(unsigned i=14;i<60;++i) assert(workspace.data[i]==0);
    fo_eth_complete(&eth,id,0);
    retry(n); /* Same PENDING request stays PENDING, even though TX finished. */
    n=send(EL_TX_RESULT,ticket,4); assert(response.payload[0]==EL_OK); retry(n);
    assert(eth.status.tx_used==7);
    assert(fo_eth_claim(&eth,&workspace,&id)); fo_eth_complete(&eth,id,-8); fo_el_put32(ticket,id);
    n=send(EL_TX_RESULT,ticket,4); assert(response.payload[0]==EL_DRIVER && fo_el_u32(response.payload+1)==UINT32_C(0xfffffff8)); retry(n);
    fo_eth_link(&eth,false,++epoch,mac);
    fo_el_put32(ticket,tickets[2]); send(EL_TX_RESULT,ticket,4); assert(response.payload[0]==EL_ABORTED);
    send(EL_RX,NULL,0); assert(response.payload[0]==EL_NO_LINK);
    fo_eth_link(&eth,true,++epoch,mac);
    for(unsigned i=0;i<8;++i) { frame[14]=(uint8_t)i; fo_eth_receive(&eth,frame,1514); }
    fo_eth_receive(&eth,frame,60); assert(eth.status.counters.rx_full==1);
    for(unsigned i=0;i<8;++i) {
        n=send(EL_RX,NULL,0); assert(n==FO_EL_PACKET && response.payload[0]==EL_OK && response.payload[15]==i);
        retry(n); assert(eth.status.rx_used==7u-i);
    }
    send(EL_RX,NULL,0); assert(response.payload[0]==EL_EMPTY);
    puts("FOE1 queues: eight async TX, credits, completion, padding, RX replay, overflow and link loss PASS");
}
static void sessions(void) {
    start(); send(EL_TX,frame,60); assert(response.payload[0]==EL_SESSION && !submits);
    seq=0; hello(); retry(53); assert(enables==1);
    uint8_t other_nonce[8]={0};
    size_t size=fo_el_pack(request,sizeof(request),EL_HELLO,0,1,0,other_nonce,8);
    size_t n=fo_el_request(&server,request,size,reply);
    assert(fo_el_unpack(reply,n,&response) && response.payload[0]==EL_SESSION && enables==1);
    n=send(EL_TX,frame,60); retry(n);
    unsigned count=submits; request[n-1]^=1; /* corrupt any byte, no operation */
    assert(!fo_el_request(&server,request,88,reply) && submits==count);
    size=fo_el_pack(request,sizeof(request),EL_TX,session,seq,epoch,frame,61);
    n=fo_el_request(&server,request,size,reply); assert(fo_el_unpack(reply,n,&response) && response.payload[0]==EL_SEQUENCE);
    size=fo_el_pack(request,sizeof(request),EL_TX,session+1,seq+1,epoch,frame,60);
    n=fo_el_request(&server,request,size,reply); assert(fo_el_unpack(reply,n,&response) && response.payload[0]==EL_SESSION);
    assert(submits==count);
    --epoch; send(EL_TX,frame,60); assert(response.payload[0]==EL_EPOCH && submits==count); ++epoch;
    race=1; send(EL_TX,frame,60); assert(response.payload[0]==EL_EPOCH); assert(!eth.status.counters.tx_ok);
    race=2; send(EL_RX,NULL,0); assert(response.payload[0]==EL_EPOCH && eth.status.rx_used==1);
    n=send(EL_RX,NULL,0); assert(response.payload[0]==EL_OK);
    uint32_t old_epoch=response.epoch;
    fo_eth_link(&eth,false,++epoch,mac); retry(n); assert(response.epoch==old_epoch);
    server.sequence=UINT32_MAX-1u; seq=UINT32_MAX-1u;
    n=send(EL_STATUS,NULL,0); assert(response.payload[0]==EL_OK); retry(n);
    size=fo_el_pack(request,sizeof(request),EL_TX,session,1,epoch,frame,60);
    n=fo_el_request(&server,request,size,reply); assert(fo_el_unpack(reply,n,&response) && response.payload[0]==EL_SEQUENCE);
    uint64_t old=session; fo_el_reset(&server); assert(!eth.status.enabled);
    session=0; seq=0; enables=0; hello(); assert(session!=old);
    n=send(EL_CLOSE,NULL,0); assert(response.payload[0]==EL_OK && !eth.status.enabled); retry(n);
    session=0; seq=0; enables=0; hello();
    puts("FOE1 recovery: changed duplicate, stale session/epoch, interleaved link change, wrap and reset PASS");
}
static void notifications(void) {
    start(); hello(); uint8_t generation[4]; uint32_t ticket;
    send(EL_STATUS,NULL,0); fo_el_put32(generation,fo_el_u32(response.payload+7));
    send(EL_ARM,generation,4); assert(response.payload[0]==EL_OK && !fo_el_take_irq(&server));
    fo_eth_receive(&eth,frame,60); assert(fo_el_take_irq(&server) && !fo_el_take_irq(&server));
    send(EL_ARM,generation,4); assert(response.payload[0]==EL_STALE);
    send(EL_STATUS,NULL,0); assert(response.payload[5]&EL_RX_READY); fo_el_put32(generation,fo_el_u32(response.payload+7));
    send(EL_ARM,generation,4); assert(response.payload[0]==EL_BUSY);
    send(EL_RX,NULL,0); send(EL_ARM,generation,4); assert(response.payload[0]==EL_OK);
    send(EL_TX,frame,60); ticket=fo_el_u32(response.payload+1);
    assert(!fo_el_take_irq(&server)); /* QUEUED is not DONE. */
    assert(fo_eth_claim(&eth,&workspace,&ticket)); fo_eth_complete(&eth,ticket,0);
    assert(fo_el_take_irq(&server)); send(EL_STATUS,NULL,0); assert(response.payload[5]&EL_TX_DONE);
    fo_el_put32(generation,fo_el_u32(response.payload+7)); send(EL_ARM,generation,4); assert(response.payload[0]==EL_BUSY);
    uint8_t bytes[4]; fo_el_put32(bytes,ticket); send(EL_TX_RESULT,bytes,4);
    send(EL_ARM,generation,4); assert(response.payload[0]==EL_OK);
    fo_eth_link(&eth,false,++epoch,mac); assert(fo_el_take_irq(&server));
    send(EL_STATUS,NULL,0); assert(response.payload[5]&EL_LINK_CHANGED);
    fo_el_put32(generation,fo_el_u32(response.payload+7)); send(EL_ARM,generation,4); assert(response.payload[0]==EL_OK);
    ++config_serial; assert(fo_el_take_irq(&server));
    send(EL_STATUS,NULL,0); assert(response.payload[5]&EL_CONFIG_DONE);
    fo_el_put32(generation,fo_el_u32(response.payload+7));
    ++config_serial; send(EL_ARM,generation,4); assert(response.payload[0]==EL_STALE); /* drain/arm race */
    server.event_generation=UINT32_MAX; ++config_serial; fo_el_poll(&server);
    fo_el_put32(generation,UINT32_MAX); send(EL_ARM,generation,4); assert(response.payload[0]==EL_STALE);
    puts("FOE1 notifications: one-shot IRQ, drain/rearm races, TX completion, config/link events, overflow PASS");
}
static void configuration(void) {
    start(); hello();
    uint8_t profile[]={5,1,1,8,'X','t','e','s','t','1','2','3','4'};
    size_t n=send(EL_CONFIG,profile,sizeof(profile));
    assert(response.payload[0]==EL_OK && response.payload[1]==1 && fo_el_u32(response.payload+2)==42);
    retry(n); assert(configs==1);
    send(EL_STATUS,NULL,0);
    for(size_t i=server.last_length;i<sizeof(server.last);++i) assert(!server.last[i]);
    for(size_t i=0;i<sizeof(server.payload);++i) assert(!server.payload[i]);
    fo_el_reset(&server); for(size_t i=0;i<sizeof(server.last);++i) assert(!server.last[i]);
    backend.config=NULL; start(); hello(); assert(!(fo_el_u32(response.payload+9)&EL_CAP_CONFIG));
    send(EL_CONFIG,profile,sizeof(profile)); assert(response.payload[0]==EL_UNSUPPORTED); backend.config=config;
    puts("FOE1 configuration: FOC1 envelope, replay, cache wiping and optional capability PASS");
}
static void hostile(void) {
    start(); hello(); size_t length=fo_el_pack(request,sizeof(request),EL_TX,session,2,epoch,frame,1514);
    for(size_t n=0;n<length;++n) assert(!fo_el_request(&server,request,n,reply));
    for(size_t i=0;i<length;++i) {
#ifdef FO_EL_M68K_TEST
        if(i!=0 && i!=4 && i!=6 && i!=7 && i!=12 && i!=20 && i!=24 && i!=length-1u) continue;
#endif
        request[i]^=0x80; assert(!fo_el_request(&server,request,length,reply)); request[i]^=0x80;
    }
    assert(submits==0 && eth.status.tx_used==0);
    uint32_t random=123;
    unsigned rounds=10000;
#ifdef FO_EL_M68K_TEST
    rounds=32;
#endif
    for(unsigned i=0;i<rounds;++i) {
        size_t n=i%FO_EL_PACKET;
        for(size_t j=0;j<n;++j) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            request[j]=(uint8_t)(random>>24);
        }
        assert(!fo_el_request(&server,request,n,reply));
    }
    assert(submits==0); send(EL_TX,frame,60); assert(response.payload[0]==EL_OK);
    puts("FOE1 malformed input: every truncation, each corrupted byte, random packets, recovery PASS");
}
int main(void) { codec(); queues(); sessions(); notifications(); configuration(); hostile(); return 0; }
