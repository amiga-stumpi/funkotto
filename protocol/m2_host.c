#include "m2_host.h"
static int wait_bits(const struct fo_m2_io *io,uint8_t mask,uint8_t wanted) {
    uint32_t start=io->ticks(io->ctx);
    uint32_t spins=0;
    for(;;) {
        if(io->cancelled(io->ctx)) return FO_HOST_CANCEL;
        if((io->status(io->ctx)&mask)==wanted) return FO_HOST_OK;
        if(((io->ticks(io->ctx)-start)&0xffffffu)>=120u) return FO_HOST_TIMEOUT;
        /* Failsafe only if the external tick source has stopped. Normal
           timing always uses ticks, never a calibrated instruction loop. */
        if(++spins==1000000u) return FO_HOST_TIMEOUT;
    }
}
int fo_m2_host_sync(const struct fo_m2_io *io) {
    io->input(io->ctx); /* Release all data outputs BEFORE requesting any read. */
    io->select(io->ctx,true);
    int r=wait_bits(io,2,2);if(r) return r;
    io->select(io->ctx,false);
    return wait_bits(io,3,0); /* POUT low + BUSY zero confirm RX ready. */
}
int fo_m2_host_exchange(const struct fo_m2_io *io,const uint8_t *request,uint8_t *reply) {
    int r=wait_bits(io,3,0);if(r) goto failed;
    io->output(io->ctx);
    uint8_t busy=0;
    for(unsigned i=0;i<FO_M2_BLOCK;i++) {
        io->write(io->ctx,request[i]); /* Exactly one PRB access -> one STROBE. */
        busy^=1u;r=wait_bits(io,3,busy);if(r) goto failed;
    }
    io->input(io->ctx);io->select(io->ctx,true);
    r=wait_bits(io,3,2);if(r) goto failed;
    busy=0;
    for(unsigned i=0;i<FO_M2_BLOCK;i++) {
        reply[i]=io->read(io->ctx); /* First byte was preloaded: no dummy read. */
        busy^=1u;r=wait_bits(io,3,(uint8_t)(2u|busy));if(r) goto failed;
    }
    io->select(io->ctx,false);
    r=wait_bits(io,3,0);
    /* Keep data inputs until the NEXT exchange, including after failures. */
    return r;
failed:
    io->input(io->ctx);io->select(io->ctx,true);return r;
}
