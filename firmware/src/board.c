#include "funkotto/board.h"
#include "hardware/gpio.h"

static void init_input(unsigned pin, bool latch) {
    gpio_init(pin);                 /* SIO, output disabled, latch low. */
    gpio_disable_pulls(pin);
    gpio_put(pin, latch);           /* Preload without enabling output. */
}

void fo_board_safe_init(void) {
    /* First actively hold both hardware enable signals low. External 4k7
       pulldowns cover reset/ROM/startup before this function runs. */
    init_input(FO_DATA_EN, false);
    gpio_set_dir(FO_DATA_EN, GPIO_OUT);
    init_input(FO_CTRL_EN, false);
    gpio_set_dir(FO_CTRL_EN, GPIO_OUT);
    for (unsigned pin = FO_DATA_FIRST; pin <= FO_DATA_LAST; ++pin)
        init_input(pin, false);
    init_input(FO_STROBE_N, false);
    init_input(FO_SEL, false);
    init_input(FO_BUSY, true);
    init_input(FO_POUT, false);
    init_input(FO_ACK_N, true);
    /* Direction changes only while data driver is disabled. */
    init_input(FO_DATA_DIR, true);
    gpio_set_dir(FO_DATA_DIR, GPIO_OUT);
}

uint32_t fo_board_output_mask(void) {
    uint32_t mask = 0;
    for (unsigned pin = 0; pin <= FO_ACK_N; ++pin) {
        if ((FO_BOARD_MASK & (UINT32_C(1) << pin)) && gpio_get_dir(pin) == GPIO_OUT)
            mask |= UINT32_C(1) << pin;
    }
    return mask;
}

bool fo_board_is_locked(void) {
    const uint32_t expected = (UINT32_C(1) << FO_DATA_DIR) |
        (UINT32_C(1) << FO_DATA_EN) | (UINT32_C(1) << FO_CTRL_EN);
    return fo_board_output_mask() == expected &&
        !gpio_get_out_level(FO_DATA_EN) && !gpio_get_out_level(FO_CTRL_EN) &&
        gpio_get_out_level(FO_DATA_DIR);
}
