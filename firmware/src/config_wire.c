#include <string.h>
#include "funkotto/config_wire.h"
#include "funkotto/raw_wire.h"
void fo_config_wire_close(struct fo_config_wire *w) { fo_wipe(w,sizeof(*w)); }
void fo_config_wire_start(struct fo_config_wire *w, uint64_t session) {
    fo_config_wire_close(w); w->session=session ? session : 1;
}
void fo_config_wire_expire_partial(struct fo_config_wire *w) {
    fo_wipe(w->input,sizeof(w->input)); w->used=0;
    /* Discard a timed-out suffix until a delimiter re-establishes a boundary. */
    w->overflow=true;
}
static size_t make_packet(uint8_t *out, uint8_t op, uint64_t session, uint32_t seq,
                          const uint8_t *payload, size_t n) {
    size_t len=fo_raw_packet(out,op,session,seq,payload,n);
    out[0]=FO_CFG_WIRE_VERSION; fo_put32(out+len-4,fo_crc32(out,len-4)); return len;
}
static size_t request(struct fo_config_wire *w, size_t n, uint8_t *out) {
    const uint8_t *d=w->decoded;
    if (n<20 || d[0]!=FO_CFG_WIRE_VERSION || n!=20u+((size_t)d[2]<<8)+d[3] ||
        fo_crc32(d,n-4)!=fo_get32(d+n-4)) return 0;
    const uint8_t op=d[1]; const uint32_t seq=fo_get32(d+4);
    const uint64_t session=(uint64_t)fo_get32(d+8)<<32 | fo_get32(d+12);
    const size_t len=n-20;
    bool hello=op==0 && seq==1 && !session && !len;
    uint8_t error=CFG_OK;
    if (!w->session || (!hello && session!=w->session)) error=CFG_SESSION;
    else if (!seq || (!w->have_request && !hello) ||
        (w->have_request && seq!=w->sequence && (w->sequence==UINT32_MAX || seq!=w->sequence+1u)) ||
        (w->have_request && seq==w->sequence && (n!=w->request_len || memcmp(d,w->request,n))) ||
        (op==0 && !(hello && (!w->have_request || w->sequence==1)))) error=CFG_SEQUENCE;
    if (error) {
        uint8_t frame[21]; size_t count=make_packet(frame,(uint8_t)(op|0x80u),w->session,seq,&error,1);
        return fo_cobs_encode(frame,count,out,FO_CFG_ENCODED);
    }
    if (w->have_request && seq==w->sequence)
        return fo_cobs_encode(w->response,w->response_len,out,FO_CFG_ENCODED);
    fo_wipe(w->request,sizeof(w->request)); memcpy(w->request,d,n); w->request_len=n;
    w->sequence=seq; w->have_request=true;
    size_t count=fo_config_exec(&w->config,hello ? CFG_INFO : op,d+16,len,w->payload);
    w->response_len=make_packet(w->response,(uint8_t)(op|0x80u),w->session,seq,w->payload,count);
    fo_wipe(w->payload,sizeof(w->payload));
    return fo_cobs_encode(w->response,w->response_len,out,FO_CFG_ENCODED);
}
size_t fo_config_wire_feed(struct fo_config_wire *w, uint8_t byte, uint8_t out[FO_CFG_ENCODED]) {
    if (byte) {
        if (!w->overflow && w->used<sizeof(w->input)) w->input[w->used++]=byte;
        else { fo_wipe(w->input,sizeof(w->input)); w->used=0; w->overflow=true; }
        return 0;
    }
    if (!w->used && !w->overflow) return 0;
    size_t n=w->overflow ? 0 : fo_cobs_decode(w->input,w->used,w->decoded,sizeof(w->decoded));
    w->used=0; w->overflow=false;
    size_t count=n ? request(w,n,out) : 0;
    fo_wipe(w->input,sizeof(w->input)); fo_wipe(w->decoded,sizeof(w->decoded));
    return count;
}
