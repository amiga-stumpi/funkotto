#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "funkotto/board.h"
#include "../firmware/src/parallel.c"
static struct {bool dir,latch,pio;} pins[30];
static struct {uint32_t rxf[4],txf[4];unsigned claimed,words;} ports[3];
static struct dma_regs regs;
struct dma_regs *dma_hw=&regs;
static bool irqs[2],channels[16],running[16];
static uint64_t now;
static int claim_limit=16;
void gpio_init(unsigned p) {pins[p].dir=false;pins[p].latch=false;pins[p].pio=false;}
void gpio_disable_pulls(unsigned p) {(void)p;}
void gpio_pull_up(unsigned p) {(void)p;}
void gpio_put(unsigned p,bool v) {
    if(p==FO_DATA_DIR) assert(!pins[FO_DATA_EN].latch);
    if(p==FO_DATA_EN && v && !pins[FO_DATA_DIR].latch) {
        assert(pins[FO_SEL].latch && irqs[0]);
        for(unsigned i=0;i<8;i++) assert(pins[i].dir&&pins[i].pio);
    }
    if(p==FO_CTRL_EN && v) assert(pins[FO_ACK_N].dir&&pins[FO_ACK_N].latch);
    pins[p].latch=v;
}
bool gpio_get(unsigned p) {return pins[p].latch;}
void gpio_set_dir(unsigned p,bool v) {pins[p].dir=v;}
unsigned gpio_get_dir(unsigned p) {return pins[p].dir;}
bool gpio_get_out_level(unsigned p) {return pins[p].latch;}
uint64_t time_us_64(void) {return now;}
void busy_wait_us_32(uint32_t n) {now+=n;}
uint32_t get_rand_32(void) {return 42;}
PIO pio_get_instance(unsigned n) {return (PIO)&ports[n];}
bool pio_sm_is_claimed(PIO p,unsigned sm) {return (p->claimed&(1u<<sm))!=0;}
bool pio_can_add_program(PIO p,const struct pio_program *pr) {return p->words+pr->length<=32;}
int pio_add_program(PIO p,const struct pio_program *pr) {unsigned off=p->words;p->words+=pr->length;return (int)off;}
void pio_remove_program(PIO p,const struct pio_program *pr,unsigned off) {(void)off;p->words-=pr->length;}
void pio_claim_sm_mask(PIO p,unsigned m) {assert(!(p->claimed&m));p->claimed|=m;}
void pio_sm_unclaim(PIO p,unsigned sm) {p->claimed&=~(1u<<sm);}
void pio_sm_set_enabled(PIO p,unsigned sm,bool e) {(void)p;(void)sm;(void)e;}
void pio_sm_clear_fifos(PIO p,unsigned sm) {(void)p;(void)sm;}
void pio_sm_restart(PIO p,unsigned sm) {(void)p;(void)sm;}
void pio_interrupt_clear(PIO p,unsigned n) {(void)p;irqs[n]=false;}
bool pio_interrupt_get(PIO p,unsigned n) {(void)p;return irqs[n];}
void pio_sm_init(PIO p,unsigned sm,unsigned off,const pio_sm_config *c) {(void)p;(void)sm;(void)off;(void)c;}
void pio_sm_set_pins_with_mask(PIO p,unsigned sm,uint32_t v,uint32_t m) {
    (void)p;(void)sm;for(unsigned i=0;i<30;i++) if(m&(1u<<i)) pins[i].latch=(v&(1u<<i))!=0;
}
void pio_sm_set_consecutive_pindirs(PIO p,unsigned sm,unsigned start,unsigned count,bool out) {
    (void)p;(void)sm;assert(!pins[FO_DATA_EN].latch);
    for(unsigned i=start;i<start+count;i++) pins[i].dir=out;
}
void pio_gpio_init(PIO p,unsigned pin) {(void)p;pins[pin].pio=true;}
void pio_sm_exec(PIO p,unsigned sm,unsigned op) {(void)p;(void)sm;(void)op;}
void pio_sm_put(PIO p,unsigned sm,uint32_t v) {(void)p;(void)sm;(void)v;}
unsigned pio_get_dreq(PIO p,unsigned sm,bool txdir) {(void)p;(void)sm;return txdir?1:0;}
int dma_claim_unused_channel(bool required) {
    (void)required;for(int i=0;i<claim_limit;i++) if(!channels[i]) {channels[i]=true;return i;}return -1;
}
void dma_channel_abort(unsigned n) {running[n]=false;}
void dma_channel_unclaim(unsigned n) {channels[n]=false;}
bool dma_channel_is_busy(unsigned n) {return running[n];}
void dma_channel_configure(unsigned n,const dma_channel_config *c,volatile void *d,const volatile void *s,uint32_t count,bool go) {
    (void)c;(void)d;(void)s;regs.ch[n].transfer_count=count;running[n]=go;
}
static void host_sel(bool v) {pins[FO_SEL].latch=v;fo_parallel_poll();}
static void idle_rx(void) {pins[FO_STROBE_N].latch=true;host_sel(true);assert(st.state==FO_PAR_SYNC);host_sel(false);assert(st.state==FO_PAR_RX);}
static void hello_frame(void) {
    uint8_t b[FO_M2_BLOCK],nonce[8]={0};assert(fo_m2_pack(b,1,0,nonce,8));
    for(unsigned i=0;i<FO_M2_BLOCK;i++) rx[i]=b[i];
    running[st.rx_dma]=false;regs.ch[st.rx_dma].transfer_count=0;irqs[1]=true;fo_parallel_poll();
    assert(st.state==FO_PAR_REQUEST);
}
int main(void) {
    fo_parallel_init();assert(fo_board_is_locked());
    if(!FUNKOTTO_M2_ACTIVE) {assert(!fo_parallel_arm());assert(fo_board_is_locked());puts("M2 default lock OK");return 0;}
    claim_limit=1;assert(!fo_parallel_arm());assert(fo_board_is_locked()&&!channels[0]&&ports[0].words==0);
    claim_limit=16;ports[0].claimed=1;assert(fo_parallel_arm());assert(st.pio_index==1);assert(!pins[FO_DATA_EN].latch);
    idle_rx();assert(pins[FO_DATA_DIR].latch);
    host_sel(true);assert(st.state==FO_PAR_SYNC&&!pins[FO_DATA_EN].latch);host_sel(false);
    hello_frame();assert(pins[FO_DATA_DIR].latch);host_sel(true);
    assert(st.state==FO_PAR_PRELOAD&&!pins[FO_DATA_EN].latch);
    irqs[0]=true;fo_parallel_poll();assert(st.state==FO_PAR_TX&&pins[FO_DATA_EN].latch&&!pins[FO_DATA_DIR].latch);
    /* Partial TX aborted by host: outputs off before RX, session invalidated. */
    host_sel(false);assert(st.state==FO_PAR_RX&&!server.active&&pins[FO_DATA_DIR].latch);
    hello_frame();host_sel(true);irqs[0]=true;fo_parallel_poll();irqs[1]=true;
    fo_parallel_poll();assert(st.state==FO_PAR_DONE);host_sel(false);assert(st.state==FO_PAR_RX&&server.active);
    now+=2000001;fo_parallel_poll();assert(st.state==FO_PAR_WAIT_SYNC&&!pins[FO_DATA_EN].latch&&!server.active);
    idle_rx();hello_frame();host_sel(true);host_sel(false);assert(st.state==FO_PAR_WAIT_SYNC&&!pins[FO_DATA_EN].latch);
    idle_rx();memset(rx,0,sizeof(rx));running[st.rx_dma]=false;irqs[1]=true;
    fo_parallel_poll();assert(st.state==FO_PAR_WAIT_SYNC&&st.invalid==1);
    fo_parallel_lock();assert(fo_board_is_locked());
    assert(ports[0].claimed==1&&ports[1].claimed==0&&ports[1].words==0);
    for(unsigned i=0;i<16;i++) assert(!channels[i]);
    puts("M2 actual transport: OE order, preload, abort, timeout, CRC, resource rollback and lock OK");
}
