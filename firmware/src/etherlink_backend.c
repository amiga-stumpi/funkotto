#include "funkotto/etherlink_usb.h"
#include "funkotto/ethernet.h"
static void enable(void *ctx, bool on) { (void)ctx; fo_net_enable(on); }
static void snapshot(void *ctx, struct fo_el_status *out) { (void)ctx; fo_net_el_snapshot(out); }
static uint8_t tx(void *ctx, uint32_t epoch, const uint8_t *data, size_t n, uint32_t *ticket) {
    (void)ctx; return fo_net_el_submit(epoch,data,n,ticket);
}
static uint8_t result(void *ctx, uint32_t ticket, int32_t *sdk_error) {
    (void)ctx; return (uint8_t)fo_net_result(ticket,sdk_error);
}
static uint8_t rx(void *ctx, uint32_t epoch, uint8_t *data, uint16_t *n) {
    (void)ctx; return fo_net_el_pop(epoch,data,n);
}
static size_t config(void *ctx, uint8_t op, const uint8_t *data, size_t n, uint8_t *out) {
    return fo_config_exec((struct fo_config *)ctx,op,data,n,out);
}
void fo_el_wifi_backend(struct fo_el_backend *io, struct fo_config *state) {
    *io=(struct fo_el_backend){state,enable,snapshot,tx,result,rx,config};
}
