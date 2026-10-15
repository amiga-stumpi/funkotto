/* ctypes harness: only SDK is fake; USB framing/FOE1/config/WLAN queues are real. */
#define main unused_service_tests
#include "test_wifi_service.c"
#undef main
#include "funkotto/etherlink_usb.h"
static struct fo_el_server bridge_server;
static struct fo_el_stream bridge_stream;
static struct fo_config bridge_config;
void bridge_init(void) {
    if(driver_up) driver_stop();
    memset(&status,0,sizeof(status));memset(&shared,0,sizeof(shared));memset(&mailbox,0,sizeof(mailbox));
    memset(flash_bytes,255,sizeof(flash_bytes));clock_ms=0;init_fail=false;link_event=false;
    flash_allowed=true;lock_depth=0;lock_hook=NULL;
    fo_wifi_launch();service_init();
    struct fo_profile p={.ssid={'T'},.ssid_len=1,.key={'1','2','3','4','5','6','7','8'},.key_len=8};
    assert(fo_wifi_submit(REQ_SET,&p));service_step();
    assert(fo_wifi_submit(REQ_CONNECT,NULL));service_step();link_event=true;service_step();
    fo_config_init(&bridge_config);fo_el_stream_clear(&bridge_stream);
    struct fo_el_backend io;fo_el_wifi_backend(&io,&bridge_config);fo_el_init(&bridge_server,&io,6789);
}
size_t bridge_exchange(const uint8_t *p, size_t n, uint8_t *out) {
    service_step(); size_t written=0;
    for(size_t i=0;i<n;++i) {
        size_t count=fo_el_stream_feed(&bridge_stream,&bridge_server,p[i],out+1);
        if(count) { assert(!written);out[0]=0;out[count+1]=0;written=count+2; }
    }
    return written;
}
uint32_t bridge_tx_queued(void) { return ethernet.status.counters.tx_queued; }
void bridge_drop_session(void) { fo_el_reset(&bridge_server);fo_el_stream_clear(&bridge_stream); }
