#include <string.h>
#include "client.h"
/* Volatile byte accesses prohibit unaligned word/long optimizations on 68000. */
uint32_t cfg_u32(const uint8_t *p) {
    const volatile uint8_t *v=p;
    return ((uint32_t)v[0]<<24)|((uint32_t)v[1]<<16)|((uint32_t)v[2]<<8)|v[3];
}
uint16_t cfg_u16(const uint8_t *p) {
    const volatile uint8_t *v=p;
    return (uint16_t)(((uint16_t)v[0]<<8)|v[1]);
}
void cfg_put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
int cfg_query(struct cfg_client *c, uint8_t op, const uint8_t *p, size_t n) {
    c->remote_result=255; c->job_reply=255;
    memset(c->response,0,sizeof(c->response));
    c->length=0;
    if (n>FO_CFG_MAX || (n && !p)) return CC_FORMAT;
    c->length=c->transport.exchange(op,p,n,c->response);
    const uint8_t *r=c->response; size_t len=c->length;
    if (!len) return CC_TRANSPORT;
    if (len>FO_CFG_MAX || r[0]>CFG_SEQUENCE) return CC_FORMAT;
    c->remote_result=r[0];
    if (r[0]!=CFG_OK && r[0]!=CFG_QUEUED) return len==1 ? CC_REMOTE : CC_FORMAT;
    if (r[0]==CFG_QUEUED) {
        if (op!=CFG_SCAN && (op<CFG_SET || op>CFG_ERASE)) return CC_FORMAT;
        return len==5 && cfg_u32(r+1) ? CC_OK : CC_FORMAT;
    }
    switch (op) {
    case CFG_INFO:
        return len==18 && !memcmp(r+1,"FOC1",4) && r[5]==1 &&
            (cfg_u32(r+7)&FO_CFG_CAPS)==FO_CFG_CAPS && cfg_u16(r+11)>=FO_CFG_MAX &&
            r[13]==32 && r[14]==8 && r[15]==63 ? CC_OK : CC_FORMAT;
    case CFG_STATUS:
        return len>=51 && r[1]<=FO_ERROR && r[2]<=FO_JOIN_FAILED &&
            r[4]<=5 && !(r[5]&252u) && r[48]<=FO_SCAN_MAX && r[49]<=1 &&
            r[50]<=32 && len==51u+r[50] ? CC_OK : CC_FORMAT;
    case CFG_SCAN_GET:
        return n==5 && len>=20 && cfg_u32(r+1)==cfg_u32(p) && r[5]==p[4] &&
            r[6]<=FO_SCAN_MAX && r[5]<r[6] && r[7]<=1 && r[19]<=32 &&
            len==20u+r[19] ? CC_OK : CC_FORMAT;
    case CFG_JOB:
        return n==4 && len==8 && cfg_u32(r+1)==cfg_u32(p) &&
            r[6]<=1 && (r[6] ? r[7]==255 : r[7]<=REPLY_FLASH) ? CC_OK : CC_FORMAT;
    default: return CC_FORMAT; /* Mutations must acknowledge with QUEUED. */
    }
}
int cfg_mutate(struct cfg_client *c, uint8_t op, const uint8_t *p, size_t n) {
    uint8_t job[4]; int result=cfg_query(c,op,p,n);
    if (result) return result;
    if (c->remote_result!=CFG_QUEUED) return CC_FORMAT;
    memcpy(job,c->response+1,4);
    /* 300 x 5 DOS ticks = 30 s. No blind resubmission on uncertainty. */
    for (unsigned i=0;i<300;++i) {
        if (!c->transport.wait()) return CC_CANCELLED;
        result=cfg_query(c,CFG_JOB,job,4);
        if (result) return result;
        if (c->response[5]!=op) return CC_FORMAT;
        if (!c->response[6]) {
            c->job_reply=c->response[7];
            return c->job_reply==REPLY_OK ? CC_OK : CC_REMOTE;
        }
    }
    return CC_TIMEOUT;
}
int cfg_scan(struct cfg_client *c) {
    int result=cfg_mutate(c,CFG_SCAN,NULL,0);
    if (result) return result;
    for (unsigned i=0;i<300;++i) {
        result=cfg_query(c,CFG_STATUS,NULL,0);
        if (result) return result;
        if (!(c->response[3]&32u)) {
            return cfg_u32(c->response+40) &&
                cfg_u32(c->response+40)==cfg_u32(c->response+44) &&
                !cfg_u32(c->response+16) ? CC_OK : CC_REMOTE;
        }
        if (!c->transport.wait()) return CC_CANCELLED;
    }
    return CC_TIMEOUT;
}
int cfg_wait_link(struct cfg_client *c) {
    for (unsigned i=0;i<300;++i) {
        int result=cfg_query(c,CFG_STATUS,NULL,0);
        if (result) return result;
        if (c->response[1]==FO_LINK_UP) return CC_OK;
        if (!(c->response[3]&4u)) return CC_REMOTE;
        if (!c->transport.wait()) return CC_CANCELLED;
    }
    return CC_TIMEOUT;
}
