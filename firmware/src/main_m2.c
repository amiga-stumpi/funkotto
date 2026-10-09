#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "funkotto/board.h"
#include "funkotto/build_info.h"
#include "funkotto/parallel.h"

static void command(const char *line) {
    if(!strcmp(line,"info")) {
        printf("FunkOtto %s source=%s stage=M2 active_build=%u wifi=disabled\n",
               FUNKOTTO_VERSION,FUNKOTTO_SOURCE_ID,(unsigned)FUNKOTTO_M2_ACTIVE);
    } else if(!strcmp(line,"status")) {
        struct fo_parallel_status s;fo_parallel_status(&s);
        printf("state=%u bus_locked=%u transactions=%lu timeouts=%lu aborts=%lu invalid=%lu"
               " pio=%d sm=%d rx_dma=%d tx_dma=%d rx_remaining=%lu tx_remaining=%lu\n",
               (unsigned)s.state,(unsigned)fo_board_is_locked(),(unsigned long)s.transactions,
               (unsigned long)s.timeouts,(unsigned long)s.aborts,(unsigned long)s.invalid,
               s.pio_index,s.sm,s.rx_dma,s.tx_dma,(unsigned long)s.rx_remaining,(unsigned long)s.tx_remaining);
    } else if(!strcmp(line,"link arm M0-VERIFIED")) {
        puts(fo_parallel_arm()?"ARMED; waiting for host sync":"LOCKED: inactive build, already armed, or no resources");
    } else if(!strcmp(line,"link lock")) { fo_parallel_lock();puts("LOCKED"); }
    else if(*line) puts("info | status | link lock | link arm M0-VERIFIED (lab build after M0 only)");
}
int main(void) {
    fo_board_safe_init(); /* Before randomness, USB, PIO allocation or logging. */
    fo_parallel_init();watchdog_enable(8000,true);stdio_init_all();
    char line[48];unsigned used=0;bool bad=false;
    command("info");
    for(;;) {
        fo_parallel_poll();
        for(unsigned i=0;i<8;i++) {
            int c=getchar_timeout_us(0);if(c<0) break;
            if(c=='\r'||c=='\n') {
                line[used]=0;
                if(!bad) command(line);else puts("ERR command");
                used=0;bad=false;
            } else if(c<32 || c>126 || used>=sizeof(line)-1u) bad=true;
            else if(!bad) line[used++]=(char)c;
        }
        watchdog_update();tight_loop_contents();
    }
}
