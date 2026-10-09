#ifndef FO_M2_HOST_H
#define FO_M2_HOST_H
#include "m2.h"
/* Backend owns misc.resource and atomic CIA register updates. ticks returns a
   monotonic 24-bit 50/60-Hz counter; no CPU-speed dependent delay loops. */
struct fo_m2_io {
    void *ctx;
    uint8_t (*status)(void *); /* bit0 BUSY, bit1 POUT */
    void (*input)(void *);
    void (*output)(void *);
    void (*select)(void *,bool);
    void (*write)(void *,uint8_t);
    uint8_t (*read)(void *);
    uint32_t (*ticks)(void *);
    bool (*cancelled)(void *);
};
enum { FO_HOST_OK=0, FO_HOST_TIMEOUT=1, FO_HOST_CANCEL=2 };
int fo_m2_host_sync(const struct fo_m2_io *io);
int fo_m2_host_exchange(const struct fo_m2_io *io,const uint8_t *request,uint8_t *reply);
#endif
