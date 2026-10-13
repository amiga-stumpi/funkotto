#include "etherlink.h"
#include <string.h>
static void wipe(void *p, size_t n) { volatile uint8_t *v=p; while(n--) *v++=0; }
uint16_t fo_el_u16(const uint8_t *p) {
    const volatile uint8_t *v=p; return (uint16_t)(((uint16_t)v[0]<<8)|v[1]);
}
uint32_t fo_el_u32(const uint8_t *p) {
    const volatile uint8_t *v=p;
    return ((uint32_t)v[0]<<24)|((uint32_t)v[1]<<16)|((uint32_t)v[2]<<8)|v[3];
}
uint64_t fo_el_u64(const uint8_t *p) { return ((uint64_t)fo_el_u32(p)<<32)|fo_el_u32(p+4); }
void fo_el_put16(uint8_t *p, uint16_t v) {
    volatile uint8_t *b=p; b[0]=(uint8_t)(v>>8); b[1]=(uint8_t)v;
}
void fo_el_put32(uint8_t *p, uint32_t v) {
    volatile uint8_t *b=p; b[0]=(uint8_t)(v>>24); b[1]=(uint8_t)(v>>16);
    b[2]=(uint8_t)(v>>8); b[3]=(uint8_t)v;
}
void fo_el_put64(uint8_t *p, uint64_t v) { fo_el_put32(p,(uint32_t)(v>>32)); fo_el_put32(p+4,(uint32_t)v); }
uint32_t fo_el_crc32(const uint8_t *p, size_t n) {
    uint32_t crc=UINT32_MAX;
    while(n--) {
        crc^=*p++;
        for(unsigned i=0;i<8;++i) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
size_t fo_el_pack(uint8_t *out, size_t cap, uint8_t op, uint64_t session,
                  uint32_t sequence, uint32_t epoch, const uint8_t *p, size_t n) {
    if (!out || n>FO_EL_PAYLOAD || cap<n+28u || (n && !p)) return 0;
    /* Volatile output avoids merged stores at odd caller addresses on 68000. */
    volatile uint8_t *v=out;
    v[0]='F'; v[1]='O'; v[2]='E'; v[3]='1'; v[4]=op; v[5]=0;
    fo_el_put16(out+6,(uint16_t)n); fo_el_put32(out+8,sequence);
    fo_el_put64(out+12,session); fo_el_put32(out+20,epoch);
    if(n) memcpy(out+24,p,n);
    fo_el_put32(out+24+n,fo_el_crc32(out,24+n)); return 28+n;
}
bool fo_el_unpack(const uint8_t *p, size_t n, struct fo_el_frame *out) {
    if (!p || !out || n<28 || n>FO_EL_PACKET || memcmp(p,"FOE1",4) || p[5]) return false;
    uint16_t length=fo_el_u16(p+6);
    if (n!=28u+length || fo_el_u32(p+n-4)!=fo_el_crc32(p,n-4)) return false;
    out->op=p[4]; out->length=length; out->sequence=fo_el_u32(p+8);
    out->session=fo_el_u64(p+12); out->epoch=fo_el_u32(p+20); out->payload=p+24;
    return true;
}
void fo_el_reset(struct fo_el_server *s) {
    s->io.enable(s->io.ctx,false);
    wipe(s->last,sizeof(s->last)); wipe(s->reply,sizeof(s->reply)); wipe(s->payload,sizeof(s->payload));
    s->active=false; s->cached=false; s->armed=false; s->irq_requested=false; s->seen_valid=false;
    s->last_length=0; s->reply_length=0; s->sequence=0; s->session=0;
    s->event_generation=1; s->latched_events=0;
}
void fo_el_init(struct fo_el_server *s, const struct fo_el_backend *io, uint64_t seed) {
    memset(s,0,sizeof(*s)); s->io=*io; s->generation=seed;
    fo_el_reset(s);
}
void fo_el_poll(struct fo_el_server *s) {
    if (!s->active) return;
    struct fo_el_status now; s->io.snapshot(s->io.ctx,&now);
    bool changed=false;
    if (s->seen_valid) {
        if (now.link!=s->seen.link || now.epoch!=s->seen.epoch || memcmp(now.mac,s->seen.mac,6)) {
            s->latched_events|=EL_LINK_CHANGED; changed=true;
        }
        if (now.config_serial!=s->seen.config_serial) { s->latched_events|=EL_CONFIG_DONE; changed=true; }
        if (now.rx_serial!=s->seen.rx_serial || now.tx_serial!=s->seen.tx_serial ||
            now.rx_count>s->seen.rx_count || now.tx_done>s->seen.tx_done) changed=true;
    }
    s->seen=now; s->seen_valid=true;
    if (changed) {
        if (s->event_generation==UINT32_MAX) s->latched_events|=EL_EVENT_OVERFLOW;
        else ++s->event_generation;
        if (s->armed) s->irq_requested=true;
    }
}
bool fo_el_take_irq(struct fo_el_server *s) {
    fo_el_poll(s);
    if (!s->active || !s->irq_requested) return false;
    s->irq_requested=false; s->armed=false; return true;
}
static uint8_t events(const struct fo_el_server *s) {
    return (uint8_t)(s->latched_events|(s->seen.rx_count ? EL_RX_READY : 0u)|
                    (s->seen.tx_done ? EL_TX_DONE : 0u));
}
static size_t error_reply(struct fo_el_server *s, const struct fo_el_frame *f,
                           uint8_t result, uint8_t *out) {
    return fo_el_pack(out,FO_EL_PACKET,(uint8_t)(f->op|128u),s->session,
                      f->sequence,s->seen_valid ? s->seen.epoch : 0,&result,1);
}
static size_t execute(struct fo_el_server *s, const struct fo_el_frame *f) {
    uint8_t *p=s->payload; const uint8_t *in=f->payload;
    size_t n=1; p[0]=EL_OK;
    switch(f->op) {
    case EL_STATUS:
        if(f->length) { p[0]=EL_INVALID; break; }
        p[1]=(uint8_t)s->seen.link; p[2]=s->seen.tx_free; p[3]=s->seen.rx_count;
        p[4]=s->seen.tx_done; p[5]=events(s); p[6]=(uint8_t)s->armed;
        fo_el_put32(p+7,s->event_generation); memcpy(p+11,s->seen.mac,6);
        fo_el_put32(p+17,s->seen.rx_serial); fo_el_put32(p+21,s->seen.tx_serial);
        fo_el_put32(p+25,s->seen.config_serial); n=29; break;
    case EL_TX:
        if(f->length<14 || f->length>FO_EL_MTU) p[0]=EL_INVALID;
        else if(f->epoch!=s->seen.epoch) p[0]=EL_EPOCH;
        else {
            uint32_t ticket=0; p[0]=s->io.tx(s->io.ctx,f->epoch,in,f->length,&ticket);
            if(p[0]==EL_OK) { fo_el_put32(p+1,ticket); n=5; }
        }
        break;
    case EL_TX_RESULT:
        if(f->length!=4 || !fo_el_u32(in)) { p[0]=EL_INVALID; break; }
        {
            int32_t sdk_error=0;
            p[0]=s->io.result(s->io.ctx,fo_el_u32(in),&sdk_error);
            fo_el_put32(p+1,(uint32_t)sdk_error); n=5;
        }
        break;
    case EL_RX:
        if(f->length) p[0]=EL_INVALID;
        else if(f->epoch!=s->seen.epoch) p[0]=EL_EPOCH;
        else {
            uint16_t length=0; p[0]=s->io.rx(s->io.ctx,f->epoch,p+1,&length);
            if(p[0]==EL_OK) {
                if(length<14 || length>FO_EL_MTU) p[0]=EL_INTERNAL;
                else n=1u+length;
            }
        }
        break;
    case EL_CONFIG:
        if(!s->io.config) p[0]=EL_UNSUPPORTED;
        else if(!f->length || f->length>FO_EL_CONFIG_MAX+1u) p[0]=EL_INVALID;
        else {
            size_t count=s->io.config(s->io.ctx,in[0],in+1,(size_t)f->length-1u,p+1);
            if(!count || count>FO_EL_CONFIG_MAX) p[0]=EL_INTERNAL;
            else n=1+count;
        }
        break;
    case EL_ARM:
        if(f->length!=4) p[0]=EL_INVALID;
        else if(fo_el_u32(in)!=s->event_generation || (s->latched_events&EL_EVENT_OVERFLOW)) p[0]=EL_STALE;
        else if(s->seen.rx_count || s->seen.tx_done) p[0]=EL_BUSY;
        else {
            s->latched_events=0; s->irq_requested=false; s->armed=true;
            fo_el_put32(p+1,s->event_generation); n=5;
        }
        break;
    case EL_CLOSE:
        if(f->length) p[0]=EL_INVALID;
        else {
            s->io.enable(s->io.ctx,false); s->active=false;
            s->armed=false; s->irq_requested=false;
        }
        break;
    default: p[0]=EL_UNSUPPORTED; break;
    }
    return n;
}
size_t fo_el_request(struct fo_el_server *s, const uint8_t *in, size_t length,
                     uint8_t out[FO_EL_PACKET]) {
    struct fo_el_frame f;
    if(!fo_el_unpack(in,length,&f) || !f.op || (f.op&128u) || !f.sequence) return 0;
    fo_el_poll(s);
    /* Byte-exact retries return the historical reply even after an epoch change.
     * CLOSE can also be replayed after disabling queues. No mutation runs twice. */
    if(s->cached && length==s->last_length && !memcmp(in,s->last,length)) {
        memcpy(out,s->reply,s->reply_length); return s->reply_length;
    }
    size_t n;
    if(f.op==EL_HELLO) {
        if(f.session || f.sequence!=1 || f.epoch || f.length!=8)
            return error_reply(s,&f,EL_INVALID,out);
        if(s->active) return error_reply(s,&f,EL_SESSION,out);
        if(!s->generation || s->generation==UINT64_MAX) return error_reply(s,&f,EL_INTERNAL,out);
        s->session=++s->generation; s->active=true; s->armed=false; s->irq_requested=false;
        s->event_generation=1; s->latched_events=0; s->seen_valid=false;
        s->io.enable(s->io.ctx,true); fo_el_poll(s);
        uint8_t *p=s->payload; p[0]=EL_OK; memcpy(p+1,f.payload,8);
        fo_el_put32(p+9,EL_CAP_ETH|EL_CAP_ASYNC_TX|EL_CAP_EVENTS|(s->io.config ? EL_CAP_CONFIG : 0u));
        fo_el_put16(p+13,FO_EL_MTU); p[15]=FO_EL_SLOTS;
        memcpy(p+16,s->seen.mac,6); p[22]=(uint8_t)s->seen.link;
        fo_el_put16(p+23,FO_EL_PAYLOAD); n=25;
    } else {
        if(!s->active || f.session!=s->session) return error_reply(s,&f,EL_SESSION,out);
        if(s->sequence==UINT32_MAX || f.sequence!=s->sequence+1u) return error_reply(s,&f,EL_SEQUENCE,out);
        n=execute(s,&f);
    }
    /* Save the operation's observed epoch, not a later snapshot: an intervening
     * link change must never relabel an RX frame as belonging to the new link. */
    wipe(s->reply,sizeof(s->reply));
    s->reply_length=fo_el_pack(s->reply,sizeof(s->reply),(uint8_t)(f.op|128u),
                               s->session,f.sequence,s->seen.epoch,s->payload,n);
    wipe(s->last,sizeof(s->last)); memcpy(s->last,in,length); s->last_length=length;
    s->sequence=f.sequence; s->cached=true;
    memcpy(out,s->reply,s->reply_length); wipe(s->payload,sizeof(s->payload));
    return s->reply_length;
}
