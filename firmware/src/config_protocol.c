#include <string.h>
#include "funkotto/config_protocol.h"
#include "funkotto/raw_wire.h"
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
void fo_config_init(struct fo_config *c) { fo_wipe(c,sizeof(*c)); }
static void refresh(struct fo_config *c) {
    fo_wifi_snapshot(&c->snapshot);
    if (c->pending && c->snapshot.completed!=c->baseline) {
        c->pending=false;
        c->job_reply=(uint8_t)(c->snapshot.completed==c->baseline+1u ? c->snapshot.reply : REPLY_DRIVER);
    }
}
size_t fo_config_exec(struct fo_config *c, uint8_t op, const uint8_t *p,
                      size_t n, uint8_t out[FO_CFG_MAX]) {
    memset(out,0,FO_CFG_MAX);
    if (n>FO_CFG_MAX || (n && !p)) { out[0]=CFG_INVALID; return 1; }
    refresh(c);
    const struct fo_wifi_status *s=&c->snapshot;
    if (op==CFG_INFO && !n) {
        memcpy(out+1,"FOC1",4); out[5]=1; out[6]=0;
        fo_put32(out+7,FO_CFG_CAPS); put16(out+11,FO_CFG_MAX);
        out[13]=32; out[14]=8; out[15]=63; out[16]='D'; out[17]='E'; return 18;
    }
    if (op==CFG_STATUS && !n) {
        out[1]=(uint8_t)s->state; out[2]=(uint8_t)s->error;
        out[3]=(uint8_t)((s->configured?1u:0u)|(s->stored?2u:0u)|(s->wanted?4u:0u)|
            (s->mac_valid?8u:0u)|(s->rssi_valid?16u:0u)|(s->scanning?32u:0u)|
            (s->sdk_busy?64u:0u)|(fo_wifi_command_busy()?128u:0u));
        out[4]=(uint8_t)s->storage_state;
        out[5]=(uint8_t)((s->flash_profile?1u:0u)|(s->flash_ready?2u:0u));
        memcpy(out+6,s->mac,6); fo_put32(out+12,(uint32_t)s->rssi);
        fo_put32(out+16,(uint32_t)s->sdk_error); fo_put32(out+20,s->epoch);
        fo_put32(out+24,s->attempts); fo_put32(out+28,s->links);
        fo_put32(out+32,s->storage_sequence); fo_put32(out+36,s->storage_error);
        fo_put32(out+40,s->scan_generation); fo_put32(out+44,s->scan_done);
        out[48]=s->scan_count; out[49]=(uint8_t)s->scan_truncated;
        out[50]=s->ssid_len; memcpy(out+51,s->ssid,s->ssid_len); return 51u+s->ssid_len;
    }
    if (op==CFG_SCAN_GET && n==5) {
        if (fo_get32(p)!=s->scan_generation || !s->scan_generation) { out[0]=CFG_STALE; return 1; }
        if (s->scanning) { out[0]=CFG_BUSY; return 1; }
        if (p[4]>=s->scan_count) { out[0]=CFG_EMPTY; return 1; }
        const struct fo_scan_result *r=&s->results[p[4]];
        fo_put32(out+1,s->scan_generation); out[5]=p[4]; out[6]=s->scan_count;
        out[7]=(uint8_t)s->scan_truncated; out[8]=r->auth;
        put16(out+9,r->channel); put16(out+11,(uint16_t)r->rssi);
        memcpy(out+13,r->bssid,6); out[19]=r->ssid_len;
        memcpy(out+20,r->ssid,r->ssid_len); return 20u+r->ssid_len;
    }
    if (op==CFG_JOB && n==4) {
        if (!c->job || fo_get32(p)!=c->job) { out[0]=CFG_STALE; return 1; }
        fo_put32(out+1,c->job); out[5]=c->job_op; out[6]=(uint8_t)c->pending;
        out[7]=c->pending ? 255u : c->job_reply; return 8;
    }
    enum fo_request req;
    struct fo_profile profile={0};
    switch(op) {
    case CFG_SCAN: req=REQ_SCAN; break;
    case CFG_SET: req=REQ_SET; break;
    case CFG_CONNECT: req=REQ_CONNECT; break;
    case CFG_DISCONNECT: req=REQ_DISCONNECT; break;
    case CFG_SAVE: req=REQ_SAVE; break;
    case CFG_ERASE: req=REQ_ERASE; break;
    default: out[0]=(op>=CFG_INFO && op<=CFG_JOB) ? CFG_INVALID : CFG_UNSUPPORTED; return 1;
    }
    if (op==CFG_SET) {
        /* WPA2-AES, followed by byte lengths, then SSID/key. No C strings. */
        if (n<3 || p[0]!=1 || p[1]>32 || p[2]>63 || n!=3u+p[1]+p[2]) {
            out[0]=CFG_INVALID; return 1;
        }
        profile.ssid_len=p[1]; profile.key_len=p[2];
        memcpy(profile.ssid,p+3,p[1]); memcpy(profile.key,p+3+p[1],p[2]);
        if (!fo_profile_valid(&profile)) { fo_wipe(&profile,sizeof(profile)); out[0]=CFG_INVALID; return 1; }
    } else if (n) { out[0]=CFG_INVALID; return 1; }
    if (c->pending || c->job==UINT32_MAX || fo_wifi_command_busy()) out[0]=CFG_BUSY;
    else if (!fo_wifi_submit(req,op==CFG_SET ? &profile : NULL)) out[0]=CFG_BUSY;
    else {
        ++c->job; c->baseline=s->completed; c->pending=true; c->job_op=op;
        out[0]=CFG_QUEUED; fo_put32(out+1,c->job);
    }
    fo_wipe(&profile,sizeof(profile)); return out[0]==CFG_QUEUED ? 5u : 1u;
}
