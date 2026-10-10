/* Deterministic WLAN backend for dispatcher/wire tests, never accesses hardware. */
#include <string.h>
#include "funkotto/config_wire.h"
#include "funkotto/ethernet.h"
static struct fo_wifi_status status;
static struct fo_profile submitted;
static enum fo_request kind;
static bool busy;
static unsigned calls, net_enables, automatic;
static struct fo_config_wire bridge;
void test_complete(unsigned reply) {
    if (!busy) return;
    if (!reply) switch(kind) {
    case REQ_SET:
        status.configured=true; status.stored=false; status.ssid_len=submitted.ssid_len;
        memcpy(status.ssid,submitted.ssid,32); break;
    case REQ_CONNECT: status.wanted=true; status.state=FO_LINK_UP; break;
    case REQ_DISCONNECT: status.wanted=false; status.state=FO_DISCONNECTED; break;
    case REQ_SCAN:
        ++status.scan_generation; status.scan_done=status.scan_generation; status.scan_count=1;
        status.results[0].ssid_len=5; memcpy(status.results[0].ssid,"Linux",5);
        status.results[0].auth=5; status.results[0].rssi=-40; status.results[0].channel=6; break;
    case REQ_SAVE: status.stored=true; status.flash_profile=true; ++status.storage_sequence; break;
    case REQ_ERASE: status.stored=false; status.configured=false; status.flash_profile=false; status.ssid_len=0; ++status.storage_sequence; break;
    }
    fo_wipe(&submitted,sizeof(submitted)); status.reply=(enum fo_reply)reply;
    ++status.completed; busy=false;
}
void fo_wifi_snapshot(struct fo_wifi_status *s) { if (automatic) test_complete(REPLY_OK); *s=status; }
bool fo_wifi_command_busy(void) { return busy; }
bool fo_wifi_submit(enum fo_request request, const struct fo_profile *profile) {
    if (busy) return false;
    kind=request; if (profile) submitted=*profile; busy=true; ++calls; return true;
}
void fo_net_enable(bool enabled) { if (enabled) ++net_enables; }
void fo_net_snapshot(struct fo_eth_status *s) { memset(s,0,sizeof(*s)); }
enum fo_net_result fo_net_submit(const uint8_t *p,size_t n,uint32_t *ticket) { (void)p; (void)n; (void)ticket; return NET_INVALID; }
enum fo_net_result fo_net_result(uint32_t ticket,int32_t *error) { (void)ticket; (void)error; return NET_INVALID; }
enum fo_net_result fo_net_pop(struct fo_frame *f) { (void)f; return NET_EMPTY; }
void test_reset(void) {
    memset(&status,0,sizeof(status)); fo_wipe(&submitted,sizeof(submitted));
    status.flash_ready=true; status.state=FO_UNCONFIGURED;
    busy=false; calls=0; net_enables=0; automatic=0;
    fo_config_wire_start(&bridge,UINT64_C(0x123456789abcdef0));
}
unsigned test_calls(void) { return calls; }
unsigned test_net_enables(void) { return net_enables; }
void test_busy(unsigned b) { busy=b!=0; }
void test_auto(unsigned a) { automatic=a; }
struct fo_wifi_status *test_status(void) { return &status; }
size_t test_feed(const uint8_t *p,size_t n,uint8_t *out) {
    size_t count=0;
    for (size_t i=0;i<n;++i) count+=fo_config_wire_feed(&bridge,p[i],out+count);
    return count;
}
