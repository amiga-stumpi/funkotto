#ifndef TEST_WIFI_SDK_H
#define TEST_WIFI_SDK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "funkotto/wifi_service.h"
typedef unsigned critical_section_t;
typedef uint64_t absolute_time_t;
typedef struct { unsigned itf_state; int join_state; bool scan_active; } cyw43_t;
extern cyw43_t cyw43_state;
typedef struct { unsigned dummy; } cyw43_wifi_scan_options_t;
typedef struct { uint8_t ssid[32], bssid[6], ssid_len, auth_mode; uint16_t channel; int16_t rssi; } cyw43_ev_scan_result_t;
#define CYW43_ITF_STA 0
#define CYW43_COUNTRY_GERMANY 0x4445
#define CYW43_NONE_PM 0
#define CYW43_AUTH_WPA2_AES_PSK 0x400004
#define CYW43_CHANNEL_NONE 0
#define CYW43_LINK_FAIL (-1)
#define CYW43_LINK_BADAUTH (-3)
#define CYW43_LINK_NONET (-2)
#define PICO_ERROR_TIMEOUT (-1)
#define NUM_PIOS 3u
#define NUM_PIO_STATE_MACHINES 4u
#define NUM_DMA_CHANNELS 16u
#define PIO_INSTANCE(p) (p)
uint64_t time_us_64(void);
void sleep_ms(unsigned n);
absolute_time_t make_timeout_time_ms(unsigned n);
void critical_section_init(critical_section_t *p);
void critical_section_enter_blocking(critical_section_t *p);
void critical_section_exit(critical_section_t *p);
void multicore_launch_core1_with_stack(void (*fn)(void), uint32_t *p, size_t n);
int cyw43_arch_init_with_country(unsigned country);
void cyw43_arch_deinit(void);
void cyw43_arch_enable_sta_mode(void);
void cyw43_arch_poll(void);
void cyw43_arch_wait_for_work_until(absolute_time_t t);
int cyw43_wifi_pm(cyw43_t *p, unsigned pm);
int cyw43_wifi_get_mac(cyw43_t *p, int itf, uint8_t *mac);
int cyw43_wifi_scan(cyw43_t *p, cyw43_wifi_scan_options_t *opts, void *env, int (*cb)(void *, const cyw43_ev_scan_result_t *));
bool cyw43_wifi_scan_active(cyw43_t *p);
int cyw43_wifi_join(cyw43_t *p, size_t n, const uint8_t *ssid, size_t k, const uint8_t *key, unsigned auth, const uint8_t *bssid, unsigned channel);
int cyw43_wifi_link_status(cyw43_t *p, int itf);
int cyw43_wifi_get_rssi(cyw43_t *p, int32_t *rssi);
bool pio_sm_is_claimed(unsigned p, unsigned sm);
bool dma_channel_is_claimed(unsigned c);
#endif
