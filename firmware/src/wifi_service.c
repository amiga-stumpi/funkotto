#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/critical_section.h"
#include "pico/cyw43_arch.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "funkotto/wifi_service.h"

static critical_section_t shared_lock;
static struct fo_wifi_status shared, status;
static struct fo_wifi_model model;
static struct { enum fo_request kind; struct fo_profile profile; bool queued, active; } mailbox;
static uint32_t core1_stack[2048] __attribute__((aligned(8)));
static bool driver_up, accept_links, pending_up, pending_down;
static uint32_t link_epoch;
static uint64_t scan_deadline, join_started, rssi_due;
static uint64_t now_ms(void) { return time_us_64() / 1000u; }
static void publish(void) {
    status.state = model.state; status.error = model.error;
    status.configured = model.configured; status.wanted = model.wanted;
    status.attempts = model.attempts; status.links = model.links; status.epoch = model.epoch;
    status.retry_at_ms = model.state == FO_RETRY_WAIT ? model.deadline_ms : 0;
    status.ssid_len = model.profile.ssid_len;
    memcpy(status.ssid, model.profile.ssid, sizeof(status.ssid));
    status.heartbeat_ms = (uint32_t)now_ms();
    critical_section_enter_blocking(&shared_lock); shared = status; critical_section_exit(&shared_lock);
}
void fo_wifi_snapshot(struct fo_wifi_status *s) {
    critical_section_enter_blocking(&shared_lock); *s = shared; critical_section_exit(&shared_lock);
}
bool fo_wifi_submit(enum fo_request request, const struct fo_profile *profile) {
    bool ok = false;
    critical_section_enter_blocking(&shared_lock);
    if (!mailbox.active) {
        mailbox.kind = request;
        if (profile) mailbox.profile = *profile;
        mailbox.active = true; mailbox.queued = true; ok = true;
    }
    critical_section_exit(&shared_lock); return ok;
}
/* These callbacks run only on Core 1 in the selected poll context. They must
 * never print, wait, reset the driver or call back into the driver. */
void cyw43_cb_tcpip_init(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_tcpip_deinit(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf) {
    (void)self; if (itf == CYW43_ITF_STA && accept_links) pending_up = true;
}
void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf) {
    (void)self; if (itf == CYW43_ITF_STA && accept_links) pending_down = true;
}
void cyw43_cb_process_ethernet(void *data, int itf, size_t len, const uint8_t *buf) {
    (void)data; (void)buf;
    if (itf != CYW43_ITF_STA || len < 14 || len > 1514) ++status.rx_invalid;
    else ++status.rx_dropped; /* Deliberately no packet transport until W3. */
}
static void resources(void) {
    status.pio_mask = 0; status.dma_mask = 0;
    for (unsigned p = 0; p < NUM_PIOS; ++p)
        for (unsigned sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
            if (pio_sm_is_claimed(PIO_INSTANCE(p), sm)) status.pio_mask |= 1u << (p * 4u + sm);
    for (unsigned c = 0; c < NUM_DMA_CHANNELS; ++c)
        if (dma_channel_is_claimed(c)) status.dma_mask |= 1u << c;
}
static void driver_stop(void) {
    accept_links = false; pending_up = false; pending_down = false;
    status.rssi_valid = false;
    if (status.scanning) { status.scanning = false; status.scan_done = status.scan_generation; }
    if (driver_up) { cyw43_arch_deinit(); driver_up = false; }
    /* SDK does not guarantee wiping its state on deinit. Clear our private instance. */
    fo_wipe(&cyw43_state, sizeof(cyw43_state));
    resources();
}
static bool driver_prepare(bool fresh) {
    if (driver_up && !fresh) return true;
    status.sdk_busy = true; publish();
    const uint64_t start = now_ms();
    driver_stop();
    int rc = cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY);
    if (!rc) {
        driver_up = true;
        cyw43_arch_enable_sta_mode();
        if (!(cyw43_state.itf_state & (1u << CYW43_ITF_STA))) rc = -1;
        if (!rc) rc = cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
        if (!rc) rc = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, status.mac);
        bool any = false; for (unsigned i = 0; i < 6; ++i) any |= status.mac[i] != 0;
        if (!rc && (!any || (status.mac[0] & 1u))) rc = -2;
    }
    status.mac_valid = rc == 0; status.sdk_error = rc;
    status.init_ms = (uint32_t)(now_ms() - start);
    if (rc) driver_stop();
    resources(); status.sdk_busy = false; publish(); return rc == 0;
}
static int scan_result(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!status.scanning || r->ssid_len > 32) return 0;
    unsigned i;
    for (i = 0; i < status.scan_count; ++i)
        if (!memcmp(status.results[i].bssid, r->bssid, 6)) break;
    if (i == FO_SCAN_MAX) { status.scan_truncated = true; return 0; }
    if (i == status.scan_count) ++status.scan_count;
    struct fo_scan_result *d = &status.results[i]; memset(d, 0, sizeof(*d));
    memcpy(d->ssid, r->ssid, r->ssid_len); memcpy(d->bssid, r->bssid, 6);
    d->ssid_len = r->ssid_len; d->auth = r->auth_mode; d->channel = r->channel; d->rssi = r->rssi;
    return 0;
}
static enum fo_reply command(enum fo_request kind, const struct fo_profile *p) {
    if (kind == REQ_DISCONNECT) {
        fo_model_disconnect(&model); driver_stop(); return REPLY_OK;
    }
    if (kind == REQ_SET) {
        if (!fo_profile_valid(p)) return REPLY_INVALID;
        fo_model_disconnect(&model); driver_stop();
        status.last_link_error = 0; status.failed_attempt = 0;
        return fo_model_profile(&model, p) ? REPLY_OK : REPLY_INVALID;
    }
    if (kind == REQ_CONNECT) {
        if (status.scanning) return REPLY_BUSY;
        return fo_model_connect(&model, now_ms()) ? REPLY_OK : REPLY_NO_PROFILE;
    }
    if (kind == REQ_SCAN) {
        if (model.wanted || status.scanning) return REPLY_BUSY;
        if (!driver_prepare(false)) { model.state = FO_ERROR; model.error = FO_DRIVER_ERROR; return REPLY_DRIVER; }
        model.state = model.configured ? FO_DISCONNECTED : FO_UNCONFIGURED; model.error = FO_OK;
        status.sdk_error = 0; status.scan_count = 0; status.scan_truncated = false;
        memset(status.results, 0, sizeof(status.results)); ++status.scan_generation;
        status.scanning = true; scan_deadline = now_ms() + 10000;
        cyw43_wifi_scan_options_t opts = {0};
        int rc = cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_result);
        if (rc) { status.sdk_error = rc; status.scanning = false; status.scan_done = status.scan_generation; return REPLY_DRIVER; }
        return REPLY_OK;
    }
    return REPLY_INVALID;
}
static void service_init(void) {
    fo_model_init(&model); model.state = FO_INITIALIZING; publish();
    if (driver_prepare(false)) model.state = FO_UNCONFIGURED;
    else { model.state = FO_ERROR; model.error = FO_DRIVER_ERROR; }
}
static void service_step(void) {
        struct fo_profile p = {0}; enum fo_request kind = REQ_DISCONNECT; bool have = false;
        critical_section_enter_blocking(&shared_lock);
        if (mailbox.queued) {
            kind = mailbox.kind; p = mailbox.profile; fo_wipe(&mailbox.profile, sizeof(mailbox.profile));
            mailbox.queued = false; have = true;
        }
        critical_section_exit(&shared_lock);
        if (have) {
            status.reply = command(kind, &p); fo_wipe(&p, sizeof(p)); ++status.completed;
            publish();
            critical_section_enter_blocking(&shared_lock); mailbox.active = false; critical_section_exit(&shared_lock);
        }
        if (driver_up) cyw43_arch_poll();
        uint64_t now = now_ms();
        if (pending_down) {
            pending_down = false; pending_up = false;
            if (model.state == FO_LINK_UP) fo_model_fail(&model, FO_LINK_LOST, now);
        }
        if (pending_up) { pending_up = false; fo_model_link_up(&model, link_epoch); status.join_ms = (uint32_t)(now - join_started); }
        if (model.state == FO_CONNECTING && driver_up) {
            int s = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
            if (s < 0) {
                status.last_link_error = s; status.failed_attempt = model.attempts;
                fo_model_fail(&model, s == CYW43_LINK_BADAUTH ? FO_BAD_AUTH :
                    s == CYW43_LINK_NONET ? FO_NO_NETWORK :
                    s == CYW43_LINK_FAIL ? FO_JOIN_FAILED : FO_DRIVER_ERROR, now);
            }
        }
        if (status.scanning) {
            if (!cyw43_wifi_scan_active(&cyw43_state)) { status.scanning = false; status.scan_done = status.scan_generation; }
            else if (now >= scan_deadline) { status.sdk_error = PICO_ERROR_TIMEOUT; driver_stop(); }
        }
        if (fo_model_tick(&model, now)) {
            /* Full reset before each attempt flushes prior firmware events. */
            if (driver_prepare(true)) {
                link_epoch = model.epoch; accept_links = true; join_started = now_ms();
                int rc = cyw43_wifi_join(&cyw43_state, model.profile.ssid_len, model.profile.ssid,
                    model.profile.key_len, model.profile.key, CYW43_AUTH_WPA2_AES_PSK, NULL, CYW43_CHANNEL_NONE);
                status.sdk_error = rc; model.deadline_ms = now_ms() + 15000;
                if (rc) fo_model_fail(&model, FO_DRIVER_ERROR, now_ms());
            } else fo_model_fail(&model, FO_DRIVER_ERROR, now_ms());
        }
        if (model.state == FO_RETRY_WAIT && driver_up) driver_stop();
        if (model.state == FO_LINK_UP && now_ms() >= rssi_due) {
            status.rssi_valid = cyw43_wifi_get_rssi(&cyw43_state, &status.rssi) == 0;
            rssi_due = now_ms() + 2000;
        }
        publish();
        if (driver_up) cyw43_arch_wait_for_work_until(make_timeout_time_ms(2));
        else sleep_ms(2);
}
static void core1_main(void) {
    service_init();
    for (;;) service_step();
}

void fo_wifi_launch(void) {
    critical_section_init(&shared_lock); shared.state = FO_INITIALIZING;
    shared.sdk_busy = true; shared.heartbeat_ms = (uint32_t)now_ms();
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
}
