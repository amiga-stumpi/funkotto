#ifndef FO_CONFIG_SIM_H
#define FO_CONFIG_SIM_H
#include "client.h"
void cfg_sim_init(void);
void cfg_sim_reboot(void);
void cfg_sim_close(void);
size_t cfg_sim_exchange(uint8_t op, const uint8_t *p, size_t n, uint8_t *out);
#endif
