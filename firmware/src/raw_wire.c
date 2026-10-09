#include <string.h>
#include "funkotto/raw_wire.h"
uint32_t fo_get32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
void fo_put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)fo_get32(p)<<32 | fo_get32(p+4); }
uint32_t fo_crc32(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    while (n--) { crc ^= *p++; for (unsigned i=0;i<8;++i) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1u))); }
    return ~crc;
}
size_t fo_cobs_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    if (cap < n + n/254u + 2u) return 0;
    size_t code_at=0, written=1; uint8_t code=1;
    for (size_t i=0;i<n;++i) {
        if (!in[i]) { out[code_at]=code; code_at=written++; code=1; }
        else { out[written++]=in[i]; if (++code==255) { out[code_at]=code; code_at=written++; code=1; } }
    }
    out[code_at]=code; return written;
}
size_t fo_cobs_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    size_t read=0, written=0;
    while (read<n) {
        unsigned code=in[read++]; if (!code || code-1u > n-read || code-1u > cap-written) return 0;
        for (unsigned i=1;i<code;++i) { if (!in[read]) return 0; out[written++]=in[read++]; }
        if (code<255 && read<n) { if (written==cap) return 0; out[written++]=0; }
    }
    return written;
}
size_t fo_raw_packet(uint8_t *out, uint8_t type, uint64_t session, uint32_t seq, const uint8_t *payload, size_t n) {
    if (n>FO_RAW_PAYLOAD) return 0;
    out[0]=1; out[1]=type; out[2]=(uint8_t)(n>>8); out[3]=(uint8_t)n;
    fo_put32(out+4,seq); fo_put32(out+8,(uint32_t)(session>>32)); fo_put32(out+12,(uint32_t)session);
    if (n) memcpy(out+16,payload,n);
    fo_put32(out+16+n,fo_crc32(out,16+n)); return 20+n;
}
void fo_raw_start(struct fo_raw *r, uint64_t session) {
    memset(r,0,sizeof(*r)); r->session=session ? session : 1; fo_net_enable(true);
}
static size_t encoded(struct fo_raw *r, uint8_t *out) {
    return fo_cobs_encode(r->response,r->response_len,out,FO_RAW_ENCODED);
}
static void reply(struct fo_raw *r, const uint8_t *payload, size_t n) {
    r->response_len=fo_raw_packet(r->response,(uint8_t)(r->request[1]|0x80u),r->session,r->sequence,payload,n);
}
size_t fo_raw_poll(struct fo_raw *r, uint8_t *out) {
    if (!r->pending) return 0;
    int32_t sdk_error=0; enum fo_net_result result=fo_net_result(r->ticket,&sdk_error);
    if (result==NET_PENDING) return 0;
    uint8_t p[5]={(uint8_t)result}; fo_put32(p+1,(uint32_t)sdk_error);
    r->pending=false; reply(r,p,sizeof(p)); return encoded(r,out);
}
static size_t request(struct fo_raw *r, size_t n, uint8_t *out) {
    uint8_t *d=r->decoded;
    size_t len=(size_t)d[2]*256u+d[3];
    if (n<20 || d[0]!=1 || len+20!=n || fo_crc32(d,n-4)!=fo_get32(d+n-4)) { ++r->bad_frames; return 0; }
    uint8_t type=d[1]; uint32_t seq=fo_get32(d+4); uint64_t session=get64(d+8);
    bool hello=type==RAW_HELLO && seq==1 && session==0 && len==0;
    if ((!hello && session!=r->session) || !seq ||
        (r->have_request && (seq!=r->sequence && (r->pending || r->sequence==UINT32_MAX || seq!=r->sequence+1u))) ||
        (!r->have_request && !hello) ||
        (r->have_request && seq==r->sequence && (n!=r->request_len || memcmp(d,r->request,n)))) {
        ++r->sequence_errors;
        uint8_t p=(uint8_t)((!hello && session!=r->session) ? NET_SESSION : NET_SEQUENCE);
        uint8_t error[21]; size_t count=fo_raw_packet(error,(uint8_t)(type|0x80u),r->session,seq,&p,1);
        return fo_cobs_encode(error,count,out,FO_RAW_ENCODED);
    }
    if (r->have_request && seq==r->sequence) { ++r->replays; return r->pending ? 0 : encoded(r,out); }
    memcpy(r->request,d,n); r->request_len=n; r->sequence=seq; r->have_request=true;
    uint8_t *p=r->payload; memset(p,0,FO_RAW_PAYLOAD); size_t count=1;
    struct fo_eth_status s;
    if (type==RAW_HELLO && len==0) {
        fo_net_snapshot(&s); memcpy(p+1,s.mac,6); p[7]=(uint8_t)s.link;
        p[8]=(uint8_t)(FO_ETH_MAX>>8); p[9]=(uint8_t)FO_ETH_MAX; p[10]=FO_ETH_SLOTS;
        fo_put32(p+11,s.epoch); count=15;
    } else if (type==RAW_STATS && len==0) {
        fo_net_snapshot(&s); p[1]=(uint8_t)s.link; p[2]=s.tx_used; p[3]=s.rx_used;
        fo_put32(p+4,s.epoch);
        const uint32_t values[]={s.counters.tx_queued,s.counters.tx_ok,s.counters.tx_error,s.counters.tx_aborted,
            s.counters.tx_busy,s.counters.tx_invalid,s.counters.rx_queued,s.counters.rx_full,
            s.counters.rx_inactive,s.counters.rx_invalid,s.counters.rx_flushed,
            s.counters.tx_high_water,s.counters.rx_high_water,r->bad_frames,r->replays,r->sequence_errors};
        for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) fo_put32(p+8+4*i,values[i]);
        count=8+sizeof(values);
    } else if (type==RAW_TX) {
        p[0]=(uint8_t)fo_net_submit(d+16,len,&r->ticket); count=5;
        if (p[0]==NET_OK) { r->pending=true; return 0; }
    } else if (type==RAW_RX && len==0) {
        p[0]=(uint8_t)fo_net_pop(&r->rx_frame);
        if (p[0]==NET_OK) { memcpy(p+1,r->rx_frame.data,r->rx_frame.len); count+=r->rx_frame.len; }
    } else p[0]=NET_INVALID;
    reply(r,p,count); return encoded(r,out);
}
size_t fo_raw_feed(struct fo_raw *r, uint8_t byte, uint8_t *out) {
    if (byte) {
        if (r->used<sizeof(r->encoded)) r->encoded[r->used++]=byte; else r->overflow=true;
        return 0;
    }
    if (!r->used && !r->overflow) return 0;
    size_t n=r->overflow ? 0 : fo_cobs_decode(r->encoded,r->used,r->decoded,sizeof(r->decoded));
    r->used=0; r->overflow=false;
    if (n<20) { ++r->bad_frames; return 0; }
    return request(r,n,out);
}
