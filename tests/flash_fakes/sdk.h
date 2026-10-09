#ifndef FLASH_TEST_SDK_H
#define FLASH_TEST_SDK_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define PICO_FLASH_SIZE_BYTES 4194304u
#define NUM_DMA_CHANNELS 16u
#define PICO_OK 0
#define XIP_BASE 0x10000000u
extern uintptr_t test_sram_base,test_sram_end;
#define SRAM_BASE test_sram_base
#define SRAM_END test_sram_end
#define __not_in_flash_func(name) name
#define hard_assert(x) assert(x)
unsigned get_core_num(void);
bool flash_safe_execute_core_init(void);
bool multicore_lockout_victim_is_initialized(unsigned core);
bool dma_channel_is_claimed(unsigned c);
bool dma_channel_is_busy(unsigned c);
void flash_range_program(uint32_t offset,const uint8_t *data,size_t n);
void flash_range_erase(uint32_t offset,size_t n);
int flash_safe_execute(void (*f)(void *),void *p,uint32_t timeout);
#endif
