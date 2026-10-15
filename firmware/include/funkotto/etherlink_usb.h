#ifndef FO_ETHERLINK_USB_H
#define FO_ETHERLINK_USB_H
#include "etherlink.h"
#include "funkotto/config_protocol.h"
#define FO_EL_ENCODED (FO_EL_PACKET + FO_EL_PACKET / 254u + 2u)
/* Core 0 owns the stream and config dispatcher. Config job state outlives USB
 * sessions. The WLAN facade below performs each operation under shared_lock. */
struct fo_el_stream {
    size_t used;
    bool discarding;
    uint32_t bad_frames;
    uint8_t input[FO_EL_ENCODED], decoded[FO_EL_PACKET], reply[FO_EL_PACKET];
};
void fo_el_stream_clear(struct fo_el_stream *w);
void fo_el_stream_expire(struct fo_el_stream *w);
size_t fo_el_stream_feed(struct fo_el_stream *w, struct fo_el_server *s,
                         uint8_t byte, uint8_t out[FO_EL_ENCODED]);
void fo_el_wifi_backend(struct fo_el_backend *io, struct fo_config *config);
void fo_net_el_snapshot(struct fo_el_status *out);
uint8_t fo_net_el_submit(uint32_t epoch, const uint8_t *data, size_t len, uint32_t *ticket);
uint8_t fo_net_el_pop(uint32_t epoch, uint8_t *data, uint16_t *len);
#endif
