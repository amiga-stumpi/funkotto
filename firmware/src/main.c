#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "funkotto/board.h"
#include "funkotto/build_info.h"
#include "funkotto/console.h"

static uint64_t safe_init_us;
static bool watchdog_boot;

static void dispatch(enum fo_command command) {
    switch (command) {
    case FO_CMD_HELP:
        puts("help | info | status (USB diagnostics only; parallel bus locked)");
        break;
    case FO_CMD_INFO:
        printf("FunkOtto %s source=%s board=pico2_w sdk=%s\n",
               FUNKOTTO_VERSION, FUNKOTTO_SOURCE_ID, FUNKOTTO_SDK_COMMIT);
        puts("stage=M1 parallel=disabled wifi=not_implemented profile=not_implemented");
        break;
    case FO_CMD_STATUS:
        printf("uptime_ms=%llu safe_init_us=%llu"
               " bus_locked=%u output_mask=0x%08x watchdog_boot=%u"
               " config_offset=0x%08x\n",
               (unsigned long long)(time_us_64() / UINT64_C(1000)),
               (unsigned long long)safe_init_us,
               (unsigned)fo_board_is_locked(), (unsigned)fo_board_output_mask(),
               (unsigned)watchdog_boot, FUNKOTTO_CONFIG_FLASH_OFFSET);
        break;
    case FO_CMD_INVALID: puts("ERR invalid command; use help"); break;
    case FO_CMD_NONE: break;
    }
}

int main(void) {
    /* No USB, WLAN, logging or terminal wait before the safe pin state. */
    fo_board_safe_init();
    safe_init_us = time_us_64();
    watchdog_boot = watchdog_caused_reboot();
    watchdog_enable(8000, true);
    stdio_init_all();
    struct fo_console console;
    fo_console_reset(&console);
    /* A disconnected host may miss this banner; info can always repeat it. */
    dispatch(FO_CMD_INFO);
    for (;;) {
        if (!fo_board_is_locked()) {
            fo_board_safe_init();
            /* Stop feeding: restart after an unexpected pin-state change. */
            for (;;) tight_loop_contents();
        }
        /* A continuous USB input stream must not monopolise the loop. */
        for (unsigned count = 0; count < 32u; ++count) {
            int byte = getchar_timeout_us(0);
            if (byte == PICO_ERROR_TIMEOUT) break;
            if (byte >= 0 && byte <= 255)
                dispatch(fo_console_feed(&console, (uint8_t)byte));
        }
        watchdog_update();
        sleep_ms(1);
    }
}
