/* Local adapter simulation. Uses the production FOC1 dispatcher unchanged.
 * A simulated flash profile lives only until this program exits. No I/O. */
#include <string.h>
#include "sim.h"
static struct fo_config dispatcher;
static struct fo_wifi_status state;
static struct fo_profile ram, flash, queued;
static enum fo_request request;
static unsigned job_ticks, link_ticks, scan_ticks;
static uint32_t sequence, storage_state;
static bool flash_valid;
uint32_t fo_get32(const uint8_t *p) { return cfg_u32(p); }
void fo_put32(uint8_t *p, uint32_t v) { cfg_put32(p,v); }
static bool same_profile(void) {
    return flash_valid && ram.ssid_len==flash.ssid_len && ram.key_len==flash.key_len &&
        !memcmp(ram.ssid,flash.ssid,ram.ssid_len) && !memcmp(ram.key,flash.key,ram.key_len);
}
static void profile_status(void) {
    state.configured=fo_profile_valid(&ram); state.stored=same_profile();
    state.ssid_len=ram.ssid_len; memcpy(state.ssid,ram.ssid,32);
    state.flash_profile=flash_valid; state.storage_sequence=sequence;
    state.storage_state=storage_state; state.flash_ready=true;
}
static void disconnect(void) {
    state.wanted=false; state.rssi_valid=false; link_ticks=0;
    state.state=state.configured ? FO_DISCONNECTED : FO_UNCONFIGURED; ++state.epoch;
}
static void connect(void) {
    state.wanted=true; state.state=FO_CONNECTING; state.rssi_valid=false;
    link_ticks=3; ++state.attempts; ++state.epoch;
}
static void finish_job(void) {
    state.reply=REPLY_OK;
    switch (request) {
    case REQ_SET:
        disconnect(); fo_wipe(&ram,sizeof(ram)); ram=queued;
        profile_status(); state.state=FO_DISCONNECTED; break;
    case REQ_CONNECT:
        if (!state.configured) state.reply=REPLY_NO_PROFILE;
        else if (state.state!=FO_LINK_UP && state.state!=FO_CONNECTING) connect();
        break;
    case REQ_DISCONNECT: disconnect(); break;
    case REQ_SCAN:
        if (state.wanted) state.reply=REPLY_BUSY;
        else { ++state.scan_generation; state.scanning=true; scan_ticks=3; }
        break;
    case REQ_SAVE:
        if (!state.configured) state.reply=REPLY_NO_PROFILE;
        else {
            bool wanted=state.wanted; disconnect();
            if (!same_profile()) { fo_wipe(&flash,sizeof(flash)); flash=ram; ++sequence; }
            flash_valid=true; storage_state=1;
            if (wanted) connect();
        }
        break;
    case REQ_ERASE:
        disconnect(); fo_wipe(&ram,sizeof(ram)); fo_wipe(&flash,sizeof(flash));
        flash_valid=false; storage_state=2; ++sequence; state.state=FO_UNCONFIGURED;
        break;
    }
    profile_status(); fo_wipe(&queued,sizeof(queued)); ++state.completed;
}
static void tick(void) {
    if (link_ticks && !--link_ticks) {
        state.state=FO_LINK_UP; state.rssi_valid=true; state.rssi=-42; ++state.links;
    }
    if (scan_ticks && !--scan_ticks) {
        static const char *const names[]={"Demo WLAN","Demo WLAN",""};
        static const uint8_t lengths[]={9,9,0};
        state.scan_count=3; state.scanning=false; state.scan_done=state.scan_generation;
        for (unsigned i=0;i<3;++i) {
            struct fo_scan_result *r=&state.results[i]; memset(r,0,sizeof(*r));
            r->ssid_len=lengths[i]; memcpy(r->ssid,names[i],r->ssid_len);
            r->bssid[0]=2; r->bssid[5]=(uint8_t)(i+1u); r->auth=5;
            r->channel=(uint16_t)(1u+5u*i); r->rssi=(int16_t)(-40-(int)i*15);
        }
    }
    if (job_ticks && !--job_ticks) finish_job();
}
void fo_wifi_snapshot(struct fo_wifi_status *s) { tick(); *s=state; }
bool fo_wifi_command_busy(void) { return job_ticks!=0; }
bool fo_wifi_submit(enum fo_request kind, const struct fo_profile *profile) {
    if (job_ticks) return false;
    request=kind; if (profile) queued=*profile; job_ticks=2; return true;
}
void cfg_sim_reboot(void) {
    fo_wipe(&state,sizeof(state)); fo_wipe(&ram,sizeof(ram)); fo_wipe(&queued,sizeof(queued));
    job_ticks=0; link_ticks=0; scan_ticks=0; fo_config_init(&dispatcher);
    state.mac_valid=true; state.mac[0]=2; state.mac[5]=1;
    if (flash_valid) ram=flash;
    profile_status(); state.state=state.configured ? FO_DISCONNECTED : FO_UNCONFIGURED;
    if (state.configured) connect();
}
void cfg_sim_init(void) {
    fo_wipe(&flash,sizeof(flash)); flash_valid=false; sequence=0; storage_state=0;
    cfg_sim_reboot();
}
void cfg_sim_close(void) {
    cfg_sim_init(); fo_wipe(&state,sizeof(state)); fo_wipe(&dispatcher,sizeof(dispatcher));
}
size_t cfg_sim_exchange(uint8_t op, const uint8_t *p, size_t n, uint8_t *out) {
    return fo_config_exec(&dispatcher,op,p,n,out);
}
