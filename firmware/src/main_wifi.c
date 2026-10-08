#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/watchdog.h"
#include "funkotto/board.h"
#include "funkotto/build_info.h"
#include "funkotto/wifi_console.h"
#include "funkotto/wifi_service.h"
static struct fo_wifi_console ui;
static struct fo_wifi_status snapshot, scan_copy;
static uint64_t safe_init_us, input_ms;
static bool watchdog_boot;
static unsigned scan_row;
static bool print_scan;
static uint64_t now_ms(void) { return time_us_64() / 1000u; }
static void mac(const uint8_t *p) { printf("%02x:%02x:%02x:%02x:%02x:%02x",p[0],p[1],p[2],p[3],p[4],p[5]); }
static void escaped(const uint8_t *p, size_t n) {
    putchar('"');
    for (size_t i = 0; i < n; ++i) {
        if (p[i] >= 32 && p[i] <= 126 && p[i] != '"' && p[i] != '\\') putchar(p[i]);
        else printf("\\x%02x", p[i]);
    }
    putchar('"');
}
static void info(void) {
    printf("FunkOtto %s source=%s board=pico2_w sdk=%s\n", FUNKOTTO_VERSION, FUNKOTTO_SOURCE_ID, FUNKOTTO_SDK_COMMIT);
    puts("stage=W1W2 parallel=disabled wifi=WPA2_2.4GHz profile=RAM_ONLY raw_ethernet=not_implemented country=DE");
}
static void wifi_status(void) {
    fo_wifi_snapshot(&snapshot);
    printf("wifi=%s error=%s sdk_error=%ld configured=%u stored=0 auto_retry=%u sdk_busy=%u ",
        fo_state_name(snapshot.state), fo_error_name(snapshot.error), (long)snapshot.sdk_error,
        (unsigned)snapshot.configured, (unsigned)snapshot.wanted, (unsigned)snapshot.sdk_busy);
    printf("mac_valid=%u mac=", (unsigned)snapshot.mac_valid); mac(snapshot.mac);
    printf(" rssi_valid=%u rssi=%ld scan=%u attempts=%lu links=%lu epoch=%lu retry_at_ms=%llu\n",
        (unsigned)snapshot.rssi_valid, (long)snapshot.rssi, (unsigned)snapshot.scanning,
        (unsigned long)snapshot.attempts, (unsigned long)snapshot.links, (unsigned long)snapshot.epoch,
        (unsigned long long)snapshot.retry_at_ms);
}
static void submit(enum fo_request r, const struct fo_profile *p) {
    puts(fo_wifi_submit(r, p) ? "QUEUED (completion follows)" : "BUSY: previous command still running");
}
static void dispatch(enum fo_ui_command c) {
    switch(c) {
    case UI_HELP:
        puts("help | info | status | wifi status | wifi scan | wifi results | wifi set | wifi sethex");
        puts("wifi connect | wifi disconnect | wifi stats; Ctrl-C cancels input; RAM profile only."); break;
    case UI_INFO: info(); break;
    case UI_STATUS:
        printf("uptime_ms=%llu safe_init_us=%llu bus_locked=%u output_mask=0x%08x watchdog_boot=%u config_offset=0x%08x\n",
            (unsigned long long)now_ms(), (unsigned long long)safe_init_us,
            (unsigned)fo_board_is_locked(), (unsigned)fo_board_output_mask(),
            (unsigned)watchdog_boot, FUNKOTTO_CONFIG_FLASH_OFFSET); break;
    case UI_WIFI_STATUS: wifi_status(); break;
    case UI_SCAN: submit(REQ_SCAN, NULL); break;
    case UI_RESULTS:
        fo_wifi_snapshot(&scan_copy); scan_row = 0; print_scan = true;
        printf("scan_results=%u scanning=%u truncated=%u generation=%lu\n", (unsigned)scan_copy.scan_count,
            (unsigned)scan_copy.scanning, (unsigned)scan_copy.scan_truncated, (unsigned long)scan_copy.scan_generation); break;
    case UI_CONNECT: submit(REQ_CONNECT, NULL); break;
    case UI_DISCONNECT: submit(REQ_DISCONNECT, NULL); break;
    case UI_PROFILE: submit(REQ_SET, &ui.profile); fo_wipe(&ui.profile, sizeof(ui.profile)); break;
    case UI_PROMPT_SSID: puts(ui.hex ? "SSID hex bytes (1..32 bytes), Enter:" : "SSID (1..32 bytes; spaces allowed), Enter:"); break;
    case UI_PROMPT_KEY: puts("WPA2 password (8..63 ASCII characters); turn terminal local echo OFF. Enter:"); break;
    case UI_CANCEL: puts("Input cancelled"); break;
    case UI_INVALID: puts("ERR invalid input; use help or restart wifi set"); break;
    case UI_NOT_IMPLEMENTED: puts("NOT_IMPLEMENTED: flash profiles arrive in W4"); break;
    case UI_STATS:
        fo_wifi_snapshot(&snapshot);
        printf("init_ms=%lu join_ms=%lu core1_age_ms=%lu rx_dropped_w12=%lu rx_invalid=%lu pio_sm_mask=0x%08lx dma_mask=0x%08lx\n",
            (unsigned long)snapshot.init_ms, (unsigned long)snapshot.join_ms,
            (unsigned long)((uint32_t)now_ms()-snapshot.heartbeat_ms),
            (unsigned long)snapshot.rx_dropped, (unsigned long)snapshot.rx_invalid,
            (unsigned long)snapshot.pio_mask, (unsigned long)snapshot.dma_mask); break;
    case UI_NONE: break;
    }
}
int main(void) {
    fo_board_safe_init(); safe_init_us = time_us_64(); watchdog_boot = watchdog_caused_reboot();
    watchdog_enable(8000, true); stdio_init_all(); fo_ui_init(&ui); fo_wifi_launch(); info();
    uint32_t completed = 0, scan_done = 0;
    bool connected_before = false;
    for (;;) {
        if (!fo_board_is_locked()) { fo_board_safe_init(); for (;;) tight_loop_contents(); }
        bool connected = stdio_usb_connected();
        if (connected_before && !connected) { fo_ui_cancel(&ui); print_scan = false; }
        connected_before = connected;
        if ((ui.phase || ui.length || ui.invalid) && now_ms() - input_ms > 60000) { fo_ui_cancel(&ui); puts("Input expired"); }
        for (unsigned n = 0; n < 32; ++n) {
            int b = getchar_timeout_us(0); if (b < 0) break;
            input_ms = now_ms();
            enum fo_ui_command c = fo_ui_feed(&ui, (uint8_t)b);
            if (c != UI_NONE) { dispatch(c); break; } /* One response per iteration. */
        }
        fo_wifi_snapshot(&snapshot);
        if (snapshot.completed != completed) {
            completed = snapshot.completed;
            static const char *const replies[] = {"OK", "BUSY", "NO_PROFILE", "DRIVER_ERROR", "INVALID"};
            printf("wifi command=%lu result=%s\n", (unsigned long)completed, replies[snapshot.reply]);
        }
        if (snapshot.scan_done != scan_done) {
            scan_done = snapshot.scan_done;
            printf("scan finished count=%u truncated=%u sdk_error=%ld; use wifi results\n",
                (unsigned)snapshot.scan_count, (unsigned)snapshot.scan_truncated, (long)snapshot.sdk_error);
        }
        if (print_scan) {
            if (scan_row >= scan_copy.scan_count) print_scan = false;
            else {
                const struct fo_scan_result *r = &scan_copy.results[scan_row++];
                printf("%u channel=%u rssi=%d auth=0x%02x bssid=", scan_row, (unsigned)r->channel, (int)r->rssi, r->auth);
                mac(r->bssid); printf(" ssid="); escaped(r->ssid, r->ssid_len); putchar('\n');
            }
        }
        if (fo_wifi_watchdog_healthy(&snapshot, (uint32_t)now_ms())) watchdog_update();
        sleep_ms(1);
    }
}
