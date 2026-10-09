#ifndef FO_PARALLEL_H
#define FO_PARALLEL_H
#include <stdbool.h>
#include <stdint.h>
enum fo_parallel_state { FO_PAR_LOCKED, FO_PAR_WAIT_SYNC, FO_PAR_SYNC,
    FO_PAR_RX, FO_PAR_REQUEST, FO_PAR_PRELOAD, FO_PAR_TX, FO_PAR_DONE };
struct fo_parallel_status {
    enum fo_parallel_state state;
    uint32_t transactions, timeouts, aborts, invalid, rx_remaining, tx_remaining;
    int pio_index, sm, rx_dma, tx_dma;
};
void fo_parallel_init(void);
bool fo_parallel_arm(void);
void fo_parallel_lock(void);
void fo_parallel_poll(void);
void fo_parallel_status(struct fo_parallel_status *out);
#endif
