#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "funkotto/profile_store.h"
static uint8_t flash_bytes[2][FO_STORE_SECTOR], baseline[2][FO_STORE_SECTOR];
static long budget=-1;static unsigned mutations;static bool corrupt_write;
static const uint8_t *read_slot(unsigned slot) { assert(slot<2);return flash_bytes[slot]; }
static bool write_bytes(uint8_t *dest,const uint8_t *src,size_t n) {
    for(size_t i=0;i<n;++i) {
        if(budget==0)return false;
        if(budget>0)--budget;
        dest[i]=src?(uint8_t)(dest[i]&src[i]):255;
    }
    if(corrupt_write && src)dest[0]^=1;
    return true;
}
static bool erase_slot(unsigned slot) { assert(slot<2);++mutations;return write_bytes(flash_bytes[slot],NULL,4096); }
static bool program(unsigned slot,unsigned page,const uint8_t *data) { assert(slot<2 && page<2);++mutations;return write_bytes(flash_bytes[slot]+256*page,data,256); }
static const struct fo_store_io io={read_slot,erase_slot,program};
static struct fo_profile a={.ssid={'A'},.key={'1','2','3','4','5','6','7','8'},.ssid_len=1,.key_len=8};
static struct fo_profile b={.ssid={'B',0,'X'},.key={'8','7','6','5','4','3','2','1'},.ssid_len=3,.key_len=8};
static struct fo_store s,reboot;
static void reset(void) { memset(flash_bytes,255,sizeof(flash_bytes));budget=-1;mutations=0;corrupt_write=false;fo_store_load(&s,&io); }
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t v=UINT32_MAX;while(n--){v^=*p++;for(unsigned i=0;i<8;++i)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}return ~v;
}
static void put32(uint8_t *p,uint32_t v) {p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void envelope(unsigned slot,uint32_t seq,unsigned schema) {
    uint8_t *p=flash_bytes[slot];p[5]=(uint8_t)schema;put32(p+8,seq);put32(p+252,crc(p,252));
    put32(p+260,seq);memcpy(p+264,p+252,4);put32(p+268,~seq);
}
static void basics(void) {
    reset();assert(s.state==STORE_EMPTY && !s.profile.key_len);
    assert(fo_store_save(&s,&a) && s.state==STORE_PROFILE && s.sequence==1 && s.active==0);
    unsigned writes=mutations;assert(fo_store_save(&s,&a) && mutations==writes);
    assert(fo_store_save(&s,&b) && s.sequence==2 && s.active==1 && fo_store_matches(&s,&b));
    fo_store_load(&reboot,&io);assert(fo_store_matches(&reboot,&b));
    assert(fo_store_erase(&s) && s.state==STORE_DELETED && !s.profile.key_len);
    for(unsigned i=0;i<4096;++i)assert(flash_bytes[1][i]==255);
    writes=mutations;assert(fo_store_erase(&s) && mutations==writes);
    assert(fo_store_save(&s,&a) && s.sequence==4);
    envelope((unsigned)s.active,UINT32_MAX,1);
    /* Remove the old slot to construct a legitimate sequence-wrap fixture. */
    memset(flash_bytes[1-s.active],255,4096);fo_store_load(&s,&io);
    assert(fo_store_save(&s,&b) && s.sequence==0 && fo_store_matches(&s,&b));
    reset();assert(fo_store_save(&s,&a));envelope(0,1,2);fo_store_load(&s,&io);
    assert(s.state==STORE_INCOMPATIBLE && !s.profile.key_len);
    writes=mutations;assert(!fo_store_save(&s,&b) && s.error==STORE_FORMAT && mutations==writes);
    assert(fo_store_erase(&s) && s.state==STORE_DELETED);
    reset();assert(fo_store_save(&s,&a));memcpy(flash_bytes[1],flash_bytes[0],4096);fo_store_load(&s,&io);
    assert(s.state==STORE_AMBIGUOUS && !s.profile.key_len);
    assert(!fo_store_save(&s,&b));assert(fo_store_erase(&s) && s.state==STORE_DELETED);
    reset();assert(fo_store_save(&s,&a));memcpy(flash_bytes[1],flash_bytes[0],4096);envelope(1,0x80000001u,1);fo_store_load(&s,&io);
    assert(s.state==STORE_AMBIGUOUS);assert(fo_store_erase(&s));
    reset();assert(fo_store_save(&s,&a));memcpy(baseline,flash_bytes,sizeof(baseline));
    for(unsigned i=0;i<512;++i) {
        memcpy(flash_bytes,baseline,sizeof(baseline));flash_bytes[0][i]^=1;fo_store_load(&s,&io);
        assert(s.state==STORE_CORRUPT && !s.profile.key_len);
    }
    reset();corrupt_write=true;assert(!fo_store_save(&s,&a) && s.error==STORE_VERIFY && s.state==STORE_CORRUPT);
}
static void powercuts(void) {
    reset();assert(fo_store_save(&s,&a));memcpy(baseline,flash_bytes,sizeof(baseline));
    /* Every byte boundary across inactive-sector erase, data and commit. */
    for(long cut=0;cut<=4608;++cut) {
        memcpy(flash_bytes,baseline,sizeof(baseline));fo_store_load(&s,&io);budget=cut;
        (void)fo_store_save(&s,&b);fo_store_load(&reboot,&io);
        assert(fo_store_matches(&reboot,&a)||fo_store_matches(&reboot,&b));
        if(cut>=4608)assert(fo_store_matches(&reboot,&b));
    }
    budget=-1;memcpy(flash_bytes,baseline,sizeof(baseline));fo_store_load(&s,&io);
    assert(fo_store_save(&s,&b));memcpy(baseline,flash_bytes,sizeof(baseline));
    /* Includes interruption of deleting the old secret after tombstone commit. */
    for(long cut=0;cut<=8704;++cut) {
        memcpy(flash_bytes,baseline,sizeof(baseline));fo_store_load(&s,&io);budget=cut;
        (void)fo_store_erase(&s);fo_store_load(&reboot,&io);
        assert(fo_store_matches(&reboot,&b)||reboot.state==STORE_DELETED);
        if(cut>=4368)assert(reboot.state==STORE_DELETED); /* meaningful commit bytes durable */
        if(reboot.state==STORE_DELETED) {
            assert(!reboot.profile.key_len);budget=-1;
            assert(fo_store_cleanup(&reboot));fo_store_load(&s,&io);assert(s.state==STORE_DELETED);
            for(unsigned i=0;i<4096;++i)assert(flash_bytes[1-s.active][i]==255);
        }
    }
    /* First-ever save interruption produces no phantom credentials. */
    for(long cut=0;cut<=4608;cut+=16) {
        reset();budget=cut;(void)fo_store_save(&s,&a);fo_store_load(&reboot,&io);
        assert(reboot.state==STORE_EMPTY||reboot.state==STORE_CORRUPT||fo_store_matches(&reboot,&a));
    }
}
int main(void) { basics();powercuts();puts("PASS: W4 journal, readback, CRC, incompatible schema, sequence wrap/ambiguity, idempotence and 13,314 byte-boundary power cuts"); }
