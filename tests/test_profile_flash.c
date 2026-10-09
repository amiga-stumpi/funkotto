#include "sdk.h"
#include "../firmware/src/profile_flash.c"
static unsigned core=1,calls,programs,erases,claimed,busy;
static bool locked=true,victim=true,safe=true;
uintptr_t test_sram_base,test_sram_end;
unsigned get_core_num(void){return core;}
bool flash_safe_execute_core_init(void){return true;}
bool multicore_lockout_victim_is_initialized(unsigned c){assert(c==0);return victim;}
bool fo_board_is_locked(void){return locked;}
bool dma_channel_is_claimed(unsigned c){return (claimed&(1u<<c))!=0;}
bool dma_channel_is_busy(unsigned c){return (busy&(1u<<c))!=0;}
void flash_range_program(uint32_t offset,const uint8_t *data,size_t n){assert(data && n==256 && (offset==0x3fe000||offset==0x3fe100||offset==0x3ff000||offset==0x3ff100));++programs;}
void flash_range_erase(uint32_t offset,size_t n){assert(n==4096 && (offset==0x3fe000||offset==0x3ff000));++erases;}
int flash_safe_execute(void (*f)(void *),void *p,uint32_t timeout){assert(timeout==1000);++calls;if(!safe)return -1;f(p);return 0;}
int main(void){
    uint8_t page[256]={0};test_sram_base=(uintptr_t)page;test_sram_end=test_sram_base+sizeof(page);
    assert(fo_flash_core_init());assert(!fo_flash_io.erase(0));
    core=0;assert(!fo_flash_open());core=1;locked=false;assert(!fo_flash_open());locked=true;
    victim=false;assert(!fo_flash_open());victim=true;claimed=1;assert(!fo_flash_open());claimed=0;
    busy=4;assert(!fo_flash_open());busy=0;assert(fo_flash_open());
    assert(!fo_flash_io.erase(2));assert(!fo_flash_io.program(0,2,page));assert(!fo_flash_io.program(0,0,(const uint8_t *)0x10000000));
    safe=false;assert(!fo_flash_io.erase(0) && erases==0);safe=true;
    assert(fo_flash_io.erase(0) && fo_flash_io.erase(1));
    assert(fo_flash_io.program(0,0,page) && fo_flash_io.program(1,1,page));
    claimed=2;assert(!fo_flash_io.erase(0));claimed=0;fo_flash_close();assert(!fo_flash_io.erase(0));
    assert(calls==5 && erases==2 && programs==2);
    return 0;
}
