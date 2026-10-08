#ifndef FUNKOTTO_BOARD_H
#define FUNKOTTO_BOARD_H
#include <stdbool.h>
#include <stdint.h>

enum {
    FO_DATA_FIRST = 0, FO_DATA_LAST = 7,
    FO_STROBE_N = 8, FO_SEL = 9, FO_BUSY = 10, FO_POUT = 11,
    FO_DATA_DIR = 20, FO_DATA_EN = 21, FO_CTRL_EN = 22, FO_ACK_N = 26
};
#define FO_DATA_MASK UINT32_C(0x000000ff)
#define FO_HOST_MASK (FO_DATA_MASK | (UINT32_C(1) << FO_STROBE_N) | \
    (UINT32_C(1) << FO_SEL) | (UINT32_C(1) << FO_BUSY) | \
    (UINT32_C(1) << FO_POUT) | (UINT32_C(1) << FO_ACK_N))
#define FO_BOARD_MASK (FO_HOST_MASK | (UINT32_C(1) << FO_DATA_DIR) | \
    (UINT32_C(1) << FO_DATA_EN) | (UINT32_C(1) << FO_CTRL_EN))

/* M1 has no API that can release either external bus driver. */
void fo_board_safe_init(void);
uint32_t fo_board_output_mask(void);
bool fo_board_is_locked(void);
#endif
