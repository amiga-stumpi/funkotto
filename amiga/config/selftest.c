#include <string.h>
#include "sim.h"
static int no_wait(void) { return 1; }
int cfg_selftest(void) {
    struct cfg_client c={{cfg_sim_exchange,no_wait},{0},0,0,0};
    uint8_t profile[20]={1,9,8,'D','e','m','o',' ','W','L','A','N',
                         't','e','s','t','1','2','3','4'};
    int failed=0;
#define CHECK(x) do { if (!(x)) { failed=__LINE__; goto end; } } while (0)
    cfg_sim_init();
    CHECK(cfg_query(&c,CFG_INFO,NULL,0)==CC_OK);
    CHECK(cfg_query(&c,CFG_STATUS,NULL,0)==CC_OK && c.response[1]==FO_UNCONFIGURED);
    CHECK(cfg_mutate(&c,CFG_CONNECT,NULL,0)==CC_REMOTE && c.job_reply==REPLY_NO_PROFILE);
    CHECK(cfg_scan(&c)==CC_OK && c.response[48]==3);
    uint8_t item[5]; cfg_put32(item,cfg_u32(c.response+40)); item[4]=2;
    CHECK(cfg_query(&c,CFG_SCAN_GET,item,5)==CC_OK && c.response[19]==0);
    CHECK(cfg_mutate(&c,CFG_SET,profile,sizeof(profile))==CC_OK);
    CHECK(cfg_mutate(&c,CFG_CONNECT,NULL,0)==CC_OK && cfg_wait_link(&c)==CC_OK);
    CHECK(cfg_scan(&c)==CC_REMOTE && c.job_reply==REPLY_BUSY);
    CHECK(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_OK && cfg_wait_link(&c)==CC_OK);
    CHECK(cfg_u32(c.response+32)==1 && (c.response[3]&3u)==3);
    CHECK(cfg_mutate(&c,CFG_SAVE,NULL,0)==CC_OK && cfg_wait_link(&c)==CC_OK);
    CHECK(cfg_u32(c.response+32)==1);
    cfg_sim_reboot();
    CHECK(cfg_query(&c,CFG_INFO,NULL,0)==CC_OK && cfg_wait_link(&c)==CC_OK);
    CHECK(c.response[50]==9 && !memcmp(c.response+51,"Demo WLAN",9));
    profile[12]='X';
    CHECK(cfg_mutate(&c,CFG_SET,profile,sizeof(profile))==CC_OK);
    CHECK(cfg_query(&c,CFG_STATUS,NULL,0)==CC_OK && !(c.response[3]&2u));
    CHECK(cfg_mutate(&c,CFG_ERASE,NULL,0)==CC_OK);
    cfg_sim_reboot();
    CHECK(cfg_query(&c,CFG_STATUS,NULL,0)==CC_OK && c.response[1]==FO_UNCONFIGURED &&
          c.response[4]==2 && cfg_u32(c.response+32)==2);
end:
    fo_wipe(profile,sizeof(profile)); fo_wipe(&c,sizeof(c)); cfg_sim_close();
    return failed;
}
