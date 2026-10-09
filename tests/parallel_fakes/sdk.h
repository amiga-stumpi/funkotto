#ifndef FO_FAKE_PARALLEL_SDK
#define FO_FAKE_PARALLEL_SDK
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned uint;
#define NUM_PIOS 3u
#define GPIO_OUT true
#define GPIO_IN false
#define PICO_PIO_VERSION 1
#define DMA_SIZE_32 2
enum {pio_x,pio_y,pio_osr};
struct pio_program { const uint16_t *instructions;uint8_t length;int8_t origin;unsigned pio_version,used_gpio_ranges; };
typedef struct { uint32_t rxf[4],txf[4];unsigned claimed,words; } *PIO;
typedef struct { unsigned unused; } pio_sm_config;
typedef struct { unsigned unused; } dma_channel_config;
struct dma_regs { struct { uint32_t transfer_count; } ch[16]; };
extern struct dma_regs *dma_hw;
void gpio_init(unsigned);
void gpio_disable_pulls(unsigned);
void gpio_pull_up(unsigned);
void gpio_put(unsigned,bool);
bool gpio_get(unsigned);
void gpio_set_dir(unsigned,bool);
unsigned gpio_get_dir(unsigned);
bool gpio_get_out_level(unsigned);
uint64_t time_us_64(void);
void busy_wait_us_32(uint32_t);
uint32_t get_rand_32(void);
PIO pio_get_instance(unsigned);
bool pio_sm_is_claimed(PIO,unsigned);
bool pio_can_add_program(PIO,const struct pio_program *);
int pio_add_program(PIO,const struct pio_program *);
void pio_remove_program(PIO,const struct pio_program *,unsigned);
void pio_claim_sm_mask(PIO,unsigned);
void pio_sm_unclaim(PIO,unsigned);
void pio_sm_set_enabled(PIO,unsigned,bool);
void pio_sm_clear_fifos(PIO,unsigned);
void pio_sm_restart(PIO,unsigned);
void pio_interrupt_clear(PIO,unsigned);
bool pio_interrupt_get(PIO,unsigned);
void pio_sm_init(PIO,unsigned,unsigned,const pio_sm_config *);
void pio_sm_set_pins_with_mask(PIO,unsigned,uint32_t,uint32_t);
void pio_sm_set_consecutive_pindirs(PIO,unsigned,unsigned,unsigned,bool);
void pio_gpio_init(PIO,unsigned);
void pio_sm_exec(PIO,unsigned,unsigned);
void pio_sm_put(PIO,unsigned,uint32_t);
unsigned pio_get_dreq(PIO,unsigned,bool);
int dma_claim_unused_channel(bool);
void dma_channel_abort(unsigned);
void dma_channel_unclaim(unsigned);
bool dma_channel_is_busy(unsigned);
void dma_channel_configure(unsigned,const dma_channel_config *,volatile void *,const volatile void *,uint32_t,bool);
static inline pio_sm_config pio_get_default_sm_config(void) {return (pio_sm_config){0};}
static inline void sm_config_set_wrap(pio_sm_config *c,unsigned a,unsigned b) {(void)c;(void)a;(void)b;}
static inline void sm_config_set_sideset(pio_sm_config *c,unsigned a,bool b,bool d) {(void)c;(void)a;(void)b;(void)d;}
static inline void sm_config_set_in_pins(pio_sm_config *c,unsigned a) {(void)c;(void)a;}
static inline void sm_config_set_in_shift(pio_sm_config *c,bool a,bool b,unsigned d) {(void)c;(void)a;(void)b;(void)d;}
static inline void sm_config_set_out_shift(pio_sm_config *c,bool a,bool b,unsigned d) {(void)c;(void)a;(void)b;(void)d;}
static inline void sm_config_set_out_pins(pio_sm_config *c,unsigned a,unsigned b) {(void)c;(void)a;(void)b;}
static inline void sm_config_set_sideset_pins(pio_sm_config *c,unsigned a) {(void)c;(void)a;}
static inline unsigned pio_encode_set(unsigned a,unsigned b) {return a+b;}
static inline unsigned pio_encode_pull(bool a,bool b) {return (unsigned)a+(unsigned)b;}
static inline unsigned pio_encode_mov(unsigned a,unsigned b) {return a+b;}
static inline dma_channel_config dma_channel_get_default_config(unsigned n) {(void)n;return (dma_channel_config){0};}
static inline void channel_config_set_transfer_data_size(dma_channel_config *c,unsigned n) {(void)c;(void)n;}
static inline void channel_config_set_read_increment(dma_channel_config *c,bool n) {(void)c;(void)n;}
static inline void channel_config_set_write_increment(dma_channel_config *c,bool n) {(void)c;(void)n;}
static inline void channel_config_set_dreq(dma_channel_config *c,unsigned n) {(void)c;(void)n;}
#endif
