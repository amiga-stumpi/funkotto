#include <string.h>
#include "funkotto/profile_store.h"
/* Stable envelope: magic[4], schema u16, kind u8, auth u8, seq u32,
 * country[2], ssid_len u8, key_len u8, ssid[32], key[63], FF through byte 251,
 * CRC32 u32. Commit page: FOCM, seq, data CRC, ~seq, then FF. Big endian. */
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t c=UINT32_MAX;
    while(n--) { c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u & (0u-(c&1u))); }
    return ~c;
}
static bool erased(const uint8_t *p,size_t n) { while(n--)if(*p++!=255)return false;return true; }
struct record { bool valid, empty; enum fo_store_state state; uint32_t seq; };
static struct record inspect(const uint8_t *p) {
    struct record r={0}; r.empty=erased(p,FO_STORE_SECTOR);
    const uint8_t *c=p+FO_STORE_PAGE;
    if(memcmp(p,"FOP4",4)||memcmp(c,"FOCM",4)||get32(p+252)!=crc32(p,252)||
       get32(c+4)!=get32(p+8)||get32(c+8)!=get32(p+252)||get32(c+12)!=~get32(p+8)||
       !erased(c+16,240)||!erased(p+512,FO_STORE_SECTOR-512))return r;
    r.valid=true;r.seq=get32(p+8);r.state=STORE_INCOMPATIBLE;
    /* Unknown schema/kind/auth/country: intact but unsupported; never guess. */
    if(p[4]!=0||p[5]!=1||p[12]!='D'||p[13]!='E')return r;
    if(p[6]==2 && p[7]==0 && p[14]==0 && p[15]==0 && erased(p+16,236)) { r.state=STORE_DELETED;return r; }
    if(p[6]!=1||p[7]!=1)return r;
    struct fo_profile profile={0};profile.ssid_len=p[14];profile.key_len=p[15];
    memcpy(profile.ssid,p+16,32);memcpy(profile.key,p+48,63);
    bool valid=fo_profile_valid(&profile);fo_wipe(&profile,sizeof(profile));
    if(!valid) { r.valid=false;return r; }
    if(!erased(p+16+p[14],32u-p[14])||!erased(p+48+p[15],63u-p[15])||!erased(p+111,141)) { r.valid=false;return r; }
    r.state=STORE_PROFILE;return r;
}
void fo_store_load(struct fo_store *s,const struct fo_store_io *io) {
    fo_wipe(s,sizeof(*s));s->io=io;s->active=-1;
    struct record a=inspect(io->read(0)),b=inspect(io->read(1)),chosen={0};
    if(a.valid && b.valid) {
        uint32_t delta=a.seq-b.seq;
        if(delta==0 || delta==0x80000000u) { s->state=STORE_AMBIGUOUS;return; }
        s->active=delta<0x80000000u?0:1;
    } else if(a.valid)s->active=0;else if(b.valid)s->active=1;
    if(s->active<0) { s->state=a.empty&&b.empty?STORE_EMPTY:STORE_CORRUPT;return; }
    chosen=s->active==0?a:b;s->state=chosen.state;s->sequence=chosen.seq;
    if(s->state==STORE_PROFILE) {
        const uint8_t *p=io->read((unsigned)s->active);
        s->profile.ssid_len=p[14];s->profile.key_len=p[15];
        memcpy(s->profile.ssid,p+16,p[14]);memcpy(s->profile.key,p+48,p[15]);
    }
}
bool fo_store_matches(const struct fo_store *s,const struct fo_profile *p) {
    return s->state==STORE_PROFILE && fo_profile_valid(p) && s->profile.ssid_len==p->ssid_len &&
        s->profile.key_len==p->key_len && !memcmp(s->profile.ssid,p->ssid,p->ssid_len) && !memcmp(s->profile.key,p->key,p->key_len);
}
static bool fail(struct fo_store *s,enum fo_store_error error) {
    const struct fo_store_io *io=s->io;fo_store_load(s,io);s->error=error;return false;
}
static bool write_record(struct fo_store *s,unsigned slot,uint32_t seq,const struct fo_profile *profile) {
    memset(s->page,255,sizeof(s->page));memset(s->commit,255,sizeof(s->commit));
    uint8_t *p=s->page,*c=s->commit;
    memcpy(p,"FOP4",4);p[4]=0;p[5]=1;p[6]=profile?1:2;p[7]=profile?1:0;
    put32(p+8,seq);p[12]='D';p[13]='E';p[14]=profile?profile->ssid_len:0;p[15]=profile?profile->key_len:0;
    if(profile) { memcpy(p+16,profile->ssid,profile->ssid_len);memcpy(p+48,profile->key,profile->key_len); }
    put32(p+252,crc32(p,252));memcpy(c,"FOCM",4);put32(c+4,seq);put32(c+8,get32(p+252));put32(c+12,~seq);
    if(!s->io->erase(slot))return fail(s,STORE_IO);
    if(!erased(s->io->read(slot),FO_STORE_SECTOR))return fail(s,STORE_VERIFY);
    if(!s->io->program(slot,0,p))return fail(s,STORE_IO);
    if(memcmp(s->io->read(slot),p,256))return fail(s,STORE_VERIFY);
    if(!s->io->program(slot,1,c))return fail(s,STORE_IO);
    if(memcmp(s->io->read(slot)+256,c,256))return fail(s,STORE_VERIFY);
    const struct fo_store_io *io=s->io;fo_store_load(s,io);
    if(s->active!=(int)slot || s->sequence!=seq || s->state!=(profile?STORE_PROFILE:STORE_DELETED))return fail(s,STORE_VERIFY);
    return true;
}
bool fo_store_save(struct fo_store *s,const struct fo_profile *profile) {
    const struct fo_store_io *io=s->io;
    /* The caller's profile must not alias s->profile; service owns separate RAM. */
    fo_store_load(s,io);
    if(!fo_profile_valid(profile) || s->state==STORE_INCOMPATIBLE || s->state==STORE_AMBIGUOUS) { s->error=STORE_FORMAT;return false; }
    if(fo_store_matches(s,profile))return true;
    unsigned slot=s->active<0?0u:(unsigned)(1-s->active);
    uint32_t seq=s->active<0?1u:s->sequence+1u;
    return write_record(s,slot,seq,profile);
}
bool fo_store_cleanup(struct fo_store *s) {
    if(s->state!=STORE_DELETED)return true;
    unsigned other=(unsigned)(1-s->active);
    if(erased(s->io->read(other),FO_STORE_SECTOR))return true;
    if(!s->io->erase(other))return fail(s,STORE_IO);
    if(!erased(s->io->read(other),FO_STORE_SECTOR))return fail(s,STORE_VERIFY);
    s->error=STORE_OK;return true;
}
bool fo_store_erase(struct fo_store *s) {
    const struct fo_store_io *io=s->io;fo_store_load(s,io);
    if(s->state==STORE_EMPTY)return true;
    if(s->state==STORE_DELETED)return fo_store_cleanup(s);
    unsigned slot=s->active<0?0u:(unsigned)(1-s->active);
    /* Explicit erase also recovers unsupported/ambiguous envelopes. Overwrite
     * slot 0 when ambiguous, with a sequence immediately newer than slot 1. */
    struct record retained=inspect(io->read(1u-slot));
    uint32_t seq=retained.valid?retained.seq+1u:1u;
    return write_record(s,slot,seq,NULL) && fo_store_cleanup(s);
}
const char *fo_store_state_name(enum fo_store_state state) {
    static const char *const names[]={"EMPTY","PROFILE","DELETED","CORRUPT","INCOMPATIBLE","AMBIGUOUS"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"INVALID";
}
