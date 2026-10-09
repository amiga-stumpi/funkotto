#include "pico/stdlib.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/dma.h"
#include "hardware/regs/addressmap.h"
#include "funkotto/board.h"
#include "funkotto/profile_store.h"
_Static_assert(FLASH_SECTOR_SIZE==FO_STORE_SECTOR && FLASH_PAGE_SIZE==FO_STORE_PAGE,"Flash geometry");
_Static_assert(PICO_FLASH_SIZE_BYTES==FO_STORE_OFFSET+2u*FO_STORE_SECTOR,"Flash layout");
static bool window_open;
struct operation { uint32_t offset; const uint8_t *data; bool program; };
static void __not_in_flash_func(flash_operation)(void *arg) {
    struct operation *op=arg;
    if(op->program)flash_range_program(op->offset,op->data,FO_STORE_PAGE);
    else flash_range_erase(op->offset,FO_STORE_SECTOR);
}
bool fo_flash_core_init(void) { return flash_safe_execute_core_init(); }
static bool quiet(void) {
    if(get_core_num()!=1 || !fo_board_is_locked() || !multicore_lockout_victim_is_initialized(0))return false;
    /* W4 has no other DMA users. Reject even idle claimed channels; never
     * abort an unknown peripheral or assume its source is outside XIP. */
    for(unsigned c=0;c<NUM_DMA_CHANNELS;++c)
        if(dma_channel_is_claimed(c)||dma_channel_is_busy(c))return false;
    return true;
}
bool fo_flash_open(void) { window_open=quiet();return window_open; }
void fo_flash_close(void) { window_open=false; }
static const uint8_t *read_slot(unsigned slot) {
    hard_assert(slot<2);return (const uint8_t *)(uintptr_t)(XIP_BASE+FO_STORE_OFFSET+slot*FO_STORE_SECTOR);
}
static bool execute(unsigned slot,unsigned page,const uint8_t *data) {
    if(!window_open || !quiet() || slot>=2 || page>=2)return false;
    /* Only caller-owned SRAM may be a programming source while XIP is off. */
    if(data && ((uintptr_t)data<SRAM_BASE || (uintptr_t)data+FO_STORE_PAGE>SRAM_END))return false;
    struct operation op={FO_STORE_OFFSET+slot*FO_STORE_SECTOR+page*FO_STORE_PAGE,data,data!=NULL};
    return flash_safe_execute(flash_operation,&op,1000)==PICO_OK;
}
static bool erase_slot(unsigned slot) { return execute(slot,0,NULL); }
static bool program_page(unsigned slot,unsigned page,const uint8_t *data) { return data && execute(slot,page,data); }
const struct fo_store_io fo_flash_io={read_slot,erase_slot,program_page};
