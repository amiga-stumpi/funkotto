#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "funkotto/board.h"
#include "funkotto/console.h"
#include "hardware/gpio.h"

static bool direction[30], latch[30], touched[30];
static unsigned outputs_enabled;
static void check_pin(unsigned pin) {
    assert(pin < 30u);
    assert(FO_BOARD_MASK & (UINT32_C(1) << pin));
    touched[pin] = true;
}
void gpio_init(unsigned pin) { check_pin(pin); direction[pin] = false; latch[pin] = false; }
void gpio_disable_pulls(unsigned pin) { check_pin(pin); }
void gpio_put(unsigned pin, bool value) {
    check_pin(pin);
    if (pin == FO_DATA_EN || pin == FO_CTRL_EN) assert(!value);
    latch[pin] = value;
}
void gpio_set_dir(unsigned pin, bool output) {
    check_pin(pin);
    if (output) {
        assert(!(FO_HOST_MASK & (UINT32_C(1) << pin)));
        assert(pin == FO_DATA_DIR || pin == FO_DATA_EN || pin == FO_CTRL_EN);
        if (pin == FO_DATA_DIR) {
            assert(latch[pin]);
            assert(direction[FO_DATA_EN] && !latch[FO_DATA_EN]);
            assert(direction[FO_CTRL_EN] && !latch[FO_CTRL_EN]);
        } else assert(!latch[pin]);
        ++outputs_enabled;
    }
    direction[pin] = output;
}
unsigned gpio_get_dir(unsigned pin) { return direction[pin] ? GPIO_OUT : GPIO_IN; }
bool gpio_get_out_level(unsigned pin) { return latch[pin]; }

static void test_board(void) {
    fo_board_safe_init();
    assert(fo_board_is_locked());
    assert(outputs_enabled == 3u);
    for (unsigned pin = 0; pin < 30; ++pin)
        assert(touched[pin] == ((FO_BOARD_MASK & (UINT32_C(1) << pin)) != 0));
    assert(latch[FO_ACK_N] && latch[FO_BUSY] && !latch[FO_POUT]);
    /* Fault detection must catch both unwanted outputs and wrong direction. */
    for (unsigned pin = FO_DATA_FIRST; pin <= FO_DATA_LAST; ++pin) {
        direction[pin] = true;
        assert(!fo_board_is_locked());
        direction[pin] = false;
    }
    latch[FO_CTRL_EN] = true; assert(!fo_board_is_locked()); latch[FO_CTRL_EN] = false;
    latch[FO_DATA_EN] = true; assert(!fo_board_is_locked()); latch[FO_DATA_EN] = false;
    latch[FO_DATA_DIR] = false; assert(!fo_board_is_locked());
    fo_board_safe_init();
    assert(fo_board_is_locked());
}

static enum fo_command feed(struct fo_console *c, const char *s) {
    enum fo_command result = FO_CMD_NONE;
    while (*s) result = fo_console_feed(c, (uint8_t)*s++);
    return result;
}
static void test_console(void) {
    struct fo_console c;
    fo_console_reset(&c);
    assert(feed(&c, "info\n") == FO_CMD_INFO);
    assert(feed(&c, "status\r") == FO_CMD_STATUS);
    assert(feed(&c, "\n") == FO_CMD_NONE); /* CRLF produces just one command. */
    assert(feed(&c, "help\n") == FO_CMD_HELP);
    assert(feed(&c, "enable\n") == FO_CMD_INVALID);
    assert(feed(&c, "info extra\n") == FO_CMD_INVALID);
    assert(feed(&c, "in") == FO_CMD_NONE);
    assert(feed(&c, "fo\n") == FO_CMD_INFO);
    for (unsigned i = 0; i < 1000; ++i) assert(fo_console_feed(&c, 'x') == FO_CMD_NONE);
    assert(feed(&c, "info\n") == FO_CMD_INVALID); /* Discard whole overflowed line. */
    assert(feed(&c, "info\n") == FO_CMD_INFO);
    assert(fo_console_feed(&c, 0) == FO_CMD_NONE);
    assert(feed(&c, "status\n") == FO_CMD_INVALID);
    for (unsigned b = 128; b < 256; ++b) {
        (void)fo_console_feed(&c, (uint8_t)b);
        assert(feed(&c, "\n") == FO_CMD_INVALID);
    }
    /* Exercise the exact boundary and arbitrary byte streams under ASan/UBSan. */
    for (unsigned n = 0; n < FO_CONSOLE_CAPACITY + 2; ++n) {
        for (unsigned i = 0; i < n; ++i) (void)fo_console_feed(&c, 'x');
        assert(feed(&c, "\n") == (n ? FO_CMD_INVALID : FO_CMD_NONE));
    }
    uint32_t random = 1;
    for (unsigned i = 0; i < 100000; ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        (void)fo_console_feed(&c, (uint8_t)(random >> 24));
        assert(c.length < FO_CONSOLE_CAPACITY);
    }
}

int main(void) {
    test_board(); test_console();
    puts("PASS: safe GPIO init/fault detection; bounded USB command parser");
    return 0;
}
