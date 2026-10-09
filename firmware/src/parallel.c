#include "funkotto/parallel.h"
#include "funkotto/board.h"
#include "m2.h"
#include "parallel.pio.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#ifndef FUNKOTTO_M2_ACTIVE
#define FUNKOTTO_M2_ACTIVE 0
#endif

static struct fo_parallel_status st;
static struct fo_m2_server server;
static PIO port;
static unsigned rx_offset, tx_offset;
static uint32_t rx[FO_M2_BLOCK], tx[FO_M2_BLOCK];
static uint8_t request[FO_M2_BLOCK], response[FO_M2_BLOCK];
static uint64_t deadline, progress_at;
static uint32_t remaining;
/* USB calls never occur inside the critical turnaround. Timeouts are measured
   independently of Amiga CPU speed, DMA completion and USB terminal presence. */
static void stop_transfer(void) {
    gpio_put(FO_DATA_EN,false); /* OE off FIRST, including fault/abort. */
    if(port) {
        pio_sm_set_enabled(port,0,false);
        if(st.rx_dma>=0) dma_channel_abort((unsigned)st.rx_dma);
        if(st.tx_dma>=0) dma_channel_abort((unsigned)st.tx_dma);
        pio_sm_clear_fifos(port,0);pio_sm_restart(port,0);
        pio_interrupt_clear(port,0);pio_interrupt_clear(port,1);
    }
    for(unsigned p=0;p<8;p++) { gpio_init(p);gpio_disable_pulls(p); }
    gpio_put(FO_DATA_DIR,true);
    gpio_init(FO_BUSY);gpio_put(FO_BUSY,true);gpio_set_dir(FO_BUSY,true);
}
static void sync_high(void) {
    stop_transfer();fo_m2_abort(&server);
    gpio_put(FO_POUT,true);st.state=FO_PAR_SYNC;
}
static void fault(void) {
    stop_transfer();fo_m2_abort(&server);
    gpio_put(FO_POUT,false);st.state=FO_PAR_WAIT_SYNC;
}
void fo_parallel_init(void) {
    memset(&st,0,sizeof(st));st.pio_index=st.sm=st.rx_dma=st.tx_dma=-1;
    port=NULL;fo_m2_init(&server,get_rand_32());fo_board_safe_init();
}
bool fo_parallel_arm(void) {
    if(!FUNKOTTO_M2_ACTIVE || st.state!=FO_PAR_LOCKED) return false;
    /* Dedicated PIO: 30 instruction words and IRQ flags 0/1. Reserve all SMs
       so another owner cannot reuse these shared flags/program space. */
    for(unsigned i=0;i<NUM_PIOS;i++) {
        PIO candidate=pio_get_instance(i);bool free=true;
        for(unsigned sm=0;sm<4;sm++) if(pio_sm_is_claimed(candidate,sm)) free=false;
        if(!free || !pio_can_add_program(candidate,&fo_parallel_rx_program)) continue;
        rx_offset=(unsigned)pio_add_program(candidate,&fo_parallel_rx_program);
        if(!pio_can_add_program(candidate,&fo_parallel_tx_program)) {
            pio_remove_program(candidate,&fo_parallel_rx_program,rx_offset);continue;
        }
        tx_offset=(unsigned)pio_add_program(candidate,&fo_parallel_tx_program);
        pio_claim_sm_mask(candidate,15);port=candidate;st.pio_index=(int)i;st.sm=0;break;
    }
    if(!port) return false;
    st.rx_dma=dma_claim_unused_channel(false);st.tx_dma=dma_claim_unused_channel(false);
    if(st.rx_dma<0 || st.tx_dma<0) { fo_parallel_lock();return false; }
    /* Prepare idle output levels and input bias before shared CTRL OE. */
    gpio_put(FO_BUSY,true);gpio_set_dir(FO_BUSY,true);
    gpio_put(FO_POUT,false);gpio_set_dir(FO_POUT,true);
    gpio_put(FO_ACK_N,true);gpio_set_dir(FO_ACK_N,true);
    gpio_pull_up(FO_STROBE_N);gpio_pull_up(FO_SEL);
    gpio_put(FO_CTRL_EN,true);st.state=FO_PAR_WAIT_SYNC;
    return true;
}
void fo_parallel_lock(void) {
    if(port) {
        stop_transfer();
        if(st.rx_dma>=0) dma_channel_unclaim((unsigned)st.rx_dma);
        if(st.tx_dma>=0) dma_channel_unclaim((unsigned)st.tx_dma);
        pio_remove_program(port,&fo_parallel_rx_program,rx_offset);
        pio_remove_program(port,&fo_parallel_tx_program,tx_offset);
        for(unsigned sm=0;sm<4;sm++) pio_sm_unclaim(port,sm);
    }
    port=NULL;st.pio_index=st.sm=st.rx_dma=st.tx_dma=-1;
    fo_board_safe_init();fo_m2_abort(&server);st.state=FO_PAR_LOCKED;
}
static void setup_sm(bool transmit) {
    stop_transfer();
    pio_sm_config c=transmit ? fo_parallel_tx_program_get_default_config(tx_offset)
                             : fo_parallel_rx_program_get_default_config(rx_offset);
    sm_config_set_in_pins(&c,0);
    sm_config_set_in_shift(&c,false,false,32);
    sm_config_set_out_shift(&c,true,false,32);
    sm_config_set_out_pins(&c,transmit?0:FO_BUSY,transmit?8:1);
    if(transmit) sm_config_set_sideset_pins(&c,FO_BUSY);
    pio_sm_init(port,0,transmit?tx_offset:rx_offset,&c);
    pio_sm_set_pins_with_mask(port,0,0,FO_DATA_MASK|(1u<<FO_BUSY));
    pio_sm_set_consecutive_pindirs(port,0,0,8,transmit);
    pio_sm_set_consecutive_pindirs(port,0,FO_BUSY,1,true);
    for(unsigned p=0;p<8;p++) pio_gpio_init(port,p);
    pio_gpio_init(port,FO_BUSY);
    pio_sm_exec(port,0,pio_encode_set(pio_x,0));
    pio_sm_put(port,0,FO_M2_BLOCK-1u);
    pio_sm_exec(port,0,pio_encode_pull(false,true));
    pio_sm_exec(port,0,pio_encode_mov(pio_y,pio_osr));
    deadline=time_us_64()+UINT64_C(10000000);
    progress_at=time_us_64();remaining=FO_M2_BLOCK;
}
static void receive(void) {
    setup_sm(false);
    dma_channel_config c=dma_channel_get_default_config((unsigned)st.rx_dma);
    channel_config_set_transfer_data_size(&c,DMA_SIZE_32);
    channel_config_set_read_increment(&c,false);channel_config_set_write_increment(&c,true);
    channel_config_set_dreq(&c,pio_get_dreq(port,0,false));
    dma_channel_configure((unsigned)st.rx_dma,&c,rx,&port->rxf[0],FO_M2_BLOCK,true);
    gpio_put(FO_DATA_DIR,true);gpio_put(FO_DATA_EN,true);
    pio_sm_set_enabled(port,0,true);
    st.state=FO_PAR_RX;
    gpio_put(FO_POUT,false); /* Released only after receiver/PIO/DMA are ready. */
}
static void transmit(void) {
    setup_sm(true);
    for(unsigned i=0;i<FO_M2_BLOCK;i++) tx[i]=response[i];
    dma_channel_config c=dma_channel_get_default_config((unsigned)st.tx_dma);
    channel_config_set_transfer_data_size(&c,DMA_SIZE_32);
    channel_config_set_read_increment(&c,true);channel_config_set_write_increment(&c,false);
    channel_config_set_dreq(&c,pio_get_dreq(port,0,true));
    dma_channel_configure((unsigned)st.tx_dma,&c,&port->txf[0],tx,FO_M2_BLOCK,true);
    pio_sm_set_enabled(port,0,true);st.state=FO_PAR_PRELOAD;
}
void fo_parallel_poll(void) {
    if(st.state==FO_PAR_LOCKED) return;
    bool sel=gpio_get(FO_SEL);
    if(st.state==FO_PAR_WAIT_SYNC) { if(sel) sync_high();return; }
    if(st.state==FO_PAR_SYNC) { if(!sel && gpio_get(FO_STROBE_N)) receive();return; }
    if(st.state==FO_PAR_RX && pio_interrupt_get(port,1) &&
       !dma_channel_is_busy((unsigned)st.rx_dma)) {
        for(unsigned i=0;i<FO_M2_BLOCK;i++) request[i]=(uint8_t)rx[i];
        if(!fo_m2_request(&server,request,response)) { ++st.invalid;fault();return; }
        st.state=FO_PAR_REQUEST;
    }
    if(st.state==FO_PAR_RX && sel) { ++st.aborts;sync_high();return; }
    if(st.state==FO_PAR_REQUEST && sel) transmit();
    if(st.state==FO_PAR_PRELOAD) {
        if(!sel) { ++st.aborts;fault();return; }
        if(pio_interrupt_get(port,0) && gpio_get(FO_STROBE_N)) {
            gpio_put(FO_DATA_DIR,false);
            /* OE dead time and first-byte setup: 2 us, independent of CPU. */
            busy_wait_us_32(2);
            if(!gpio_get(FO_SEL)) { ++st.aborts;fault();return; }
            gpio_put(FO_DATA_EN,true);gpio_put(FO_POUT,true);st.state=FO_PAR_TX;
        }
    }
    if(st.state==FO_PAR_TX && pio_interrupt_get(port,1)) {
        ++st.transactions;st.state=FO_PAR_DONE;
    }
    if((st.state==FO_PAR_TX || st.state==FO_PAR_DONE) && !sel) {
        if(st.state!=FO_PAR_DONE) { ++st.aborts;fo_m2_abort(&server); }
        receive();return;
    }
    uint32_t n=st.state==FO_PAR_RX ? dma_hw->ch[st.rx_dma].transfer_count :
        dma_hw->ch[st.tx_dma].transfer_count;
    uint64_t now=time_us_64();
    if(n!=remaining) { remaining=n;progress_at=now; }
    if(now>=deadline || now-progress_at>=UINT64_C(2000000)) {
        ++st.timeouts;fault();
    }
}
void fo_parallel_status(struct fo_parallel_status *out) {
    *out=st;
    if(port) {
        out->rx_remaining=dma_hw->ch[st.rx_dma].transfer_count;
        out->tx_remaining=dma_hw->ch[st.tx_dma].transfer_count;
    }
}
