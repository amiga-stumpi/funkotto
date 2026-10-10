#include <string.h>
#include "sim.h"
#include "os.h"
#ifndef FO_DIAG_SOURCE
#define FO_DIAG_SOURCE "development"
#endif
void *SysBase,*DOSBase,*MiscBase;
extern uint32_t cfg_os_open(const char *name, uint32_t mode);
extern int32_t cfg_os_close(uint32_t handle);
extern int32_t cfg_os_read(uint32_t handle, void *p, uint32_t n);
extern void cfg_os_delay(uint32_t ticks);
extern int cfg_selftest(void);
static uint32_t output, console;
static bool io_failed, cancelled;
static struct cfg_client client;
static uint8_t line[96], profile[98];
static void put(const char *s) {
    uint32_t n=0; while (s[n]) ++n;
    if (!io_failed && os_write(output,s,n)!=(int32_t)n) io_failed=true;
}
static void number(uint32_t value) {
    static const uint32_t powers[]={1000000000u,100000000u,10000000u,1000000u,
        100000u,10000u,1000u,100u,10u,1u};
    bool started=false;
    for (unsigned i=0;i<10;++i) {
        char digit='0'; while (value>=powers[i]) { value-=powers[i]; ++digit; }
        if (digit!='0' || started || i==9) {
            char s[2]={digit,0}; put(s); started=true;
        }
    }
}
static void signed_number(int32_t value) {
    if (value<0) { put("-"); number(0u-(uint32_t)value); } else number((uint32_t)value);
}
static void hex(uint8_t b) {
    static const char digits[]="0123456789abcdef";
    char s[3]={digits[b>>4],digits[b&15u],0}; put(s);
}
static void bytes(const uint8_t *p, unsigned n) {
    put("\"");
    for (unsigned i=0;i<n;++i) {
        uint8_t b=p[i];
        if (b>=32 && b<=126 && b!='"' && b!='\\') { char s[2]={(char)b,0}; put(s); }
        else { put("\\x"); hex(b); }
    }
    put("\"");
}
static void mac(const uint8_t *p) {
    for (unsigned i=0;i<6;++i) { if (i) put(":"); hex(p[i]); }
}
static bool equal(const uint8_t *p, unsigned n, const char *s) {
    unsigned i=0;
    for (;i<n && s[i];++i) {
        unsigned c=p[i]; if (c>='a' && c<='z') c-=32;
        if (c!=(unsigned char)s[i]) return false;
    }
    return i==n && !s[i];
}
static int wait_tick(void) {
    if (cancelled || io_failed || (os_signals()&4096u)) { cancelled=true; return 0; }
    cfg_os_delay(5); return 1;
}
/* RAW: owns its echo. Reject an entire overflowing/control-sequence line;
 * never send silently truncated credentials. No history or password echo. */
static int read_line(unsigned maximum, bool secret) {
    unsigned n=0; bool bad=false;
    fo_wipe(line,sizeof(line));
    while (!io_failed) {
        uint8_t ch=0;
        if ((os_signals()&4096u) || cfg_os_read(console,&ch,1)!=1 || ch==3 || ch==4) {
            cancelled=true; fo_wipe(line,sizeof(line)); return -1;
        }
        if (ch=='\r' || ch=='\n') { put("\n"); return bad ? -2 : (int)n; }
        if (ch==8 || ch==127) {
            if (n && !bad) { line[--n]=0; if (!secret) put("\b \b"); }
        } else if (ch>=32 && ch<=126) {
            if (n>=maximum) bad=true;
            if (!bad) { line[n++]=ch; if (!secret) { char s[2]={(char)ch,0}; put(s); } }
        } else bad=true;
    }
    return -1;
}
static void result(int value) {
    if (!value) { put("OK\n"); return; }
    put("ERROR client="); signed_number(value);
    put(" protocol="); number(client.remote_result);
    put(" job_reply="); number(client.job_reply);
    put(" (timeout/cancel: operation may already have completed)\n");
}
static void status(void) {
    int rc=cfg_query(&client,CFG_STATUS,NULL,0);
    if (rc) { result(rc); return; }
    const uint8_t *r=client.response;
    put("SIM state="); put(fo_state_name((enum fo_wifi_state)r[1]));
    put(" error="); put(fo_error_name((enum fo_wifi_error)r[2]));
    put(" configured="); number(r[3]&1u); put(" stored="); number((r[3]>>1)&1u);
    put(" auto_retry="); number((r[3]>>2)&1u); put("\nSSID="); bytes(r+51,r[50]);
    put(" MAC="); mac(r+6); put(" RSSI=");
    if (r[3]&16u) signed_number((int32_t)cfg_u32(r+12)); else put("n/a");
    put("\nflash_state="); number(r[4]); put(" sequence="); number(cfg_u32(r+32));
    put(" epoch="); number(cfg_u32(r+20)); put(" links="); number(cfg_u32(r+28)); put("\n");
}
static void scan(void) {
    int rc=cfg_scan(&client); if (rc) { result(rc); put("Use disconnect before scan.\n"); return; }
    uint8_t request[5]; cfg_put32(request,cfg_u32(client.response+40));
    unsigned count=client.response[48];
    put("SIM scan count="); number(count); put(" truncated="); number(client.response[49]); put("\n");
    for (unsigned i=0;i<count;++i) {
        request[4]=(uint8_t)i; rc=cfg_query(&client,CFG_SCAN_GET,request,5);
        if (rc) { result(rc); return; }
        const uint8_t *r=client.response;
        number(i+1u); put(" SSID="); bytes(r+20,r[19]); put(" BSSID="); mac(r+13);
        put(" channel="); number(cfg_u16(r+9)); put(" RSSI="); signed_number((int16_t)cfg_u16(r+11));
        put(" auth=0x"); hex(r[8]); put("\n");
    }
}
static void set_profile(void) {
    fo_wipe(profile,sizeof(profile));
    put("SSID (1..32 ASCII bytes, spaces allowed): ");
    int n=read_line(32,false); if (n<1) goto invalid;
    profile[0]=1; profile[1]=(uint8_t)n; memcpy(profile+3,line,(size_t)n);
    put("Test WPA2 password (8..63 ASCII bytes, hidden): ");
    n=read_line(63,true); if (n<8) goto invalid;
    profile[2]=(uint8_t)n; memcpy(profile+3+profile[1],line,(size_t)n);
    fo_wipe(line,sizeof(line));
    result(cfg_mutate(&client,CFG_SET,profile,3u+profile[1]+profile[2]));
    fo_wipe(profile,sizeof(profile)); return;
invalid:
    fo_wipe(profile,sizeof(profile)); fo_wipe(line,sizeof(line));
    if (!cancelled) put("Invalid input; profile unchanged.\n");
}
static bool confirm(void) {
    put("Simulation only. Type YES to confirm: ");
    int n=read_line(8,false); return n==3 && equal(line,3,"YES");
}
static void help(void) {
    put("status scan set connect disconnect save erase reboot help quit\n"
        "SIM ONLY: no Pico, no WLAN, no CIA access, no disk writes.\n"
        "Use a made-up password. Simulated flash survives reboot, not quit.\n"
        "Input: Return, Backspace; Ctrl-C/Ctrl-D quit. No cursor editing.\n");
}
int diag_main(const uint8_t *args, uint32_t length) {
    DOSBase=os_open_library("dos.library",34); if (!DOSBase) return 20;
    output=os_output(); int exit_code=0;
    while (length && (*args==' ' || *args=='\t')) { ++args; --length; }
    while (length && (args[length-1]==' ' || args[length-1]=='\n' || args[length-1]=='\r')) --length;
    put("FunkOttoConfig M4 / 68000 / OS 1.3 source=" FO_DIAG_SOURCE "\n");
    if (equal(args,length,"SELFTEST")) {
        int failed=cfg_selftest();
        if (failed) { put("FAIL local FOC1 selftest line="); number((uint32_t)failed); put("\n"); exit_code=20; }
        else put("PASS: local FOC1 configuration selftest; no CIA access.\n");
    } else if (equal(args,length,"SIM")) {
        console=cfg_os_open("RAW:0/12/640/180/FunkOttoConfig SIM",1005);
        if (!console) { put("Cannot open RAW console.\n"); exit_code=20; }
        else {
            output=console; client.transport.exchange=cfg_sim_exchange; client.transport.wait=wait_tick;
            cfg_sim_init(); help();
            int rc=cfg_query(&client,CFG_INFO,NULL,0);
            if (rc) { result(rc); exit_code=20; }
            else while (!cancelled && !io_failed) {
                put("SIM> "); int n=read_line(20,false);
                if (n==-1) break;
                if (n==-2) { put("Invalid/too long input.\n"); continue; }
                if (!n) continue;
                if (equal(line,(unsigned)n,"QUIT")) break;
                if (equal(line,(unsigned)n,"HELP")) help();
                else if (equal(line,(unsigned)n,"STATUS")) status();
                else if (equal(line,(unsigned)n,"SCAN")) scan();
                else if (equal(line,(unsigned)n,"SET")) set_profile();
                else if (equal(line,(unsigned)n,"CONNECT")) {
                    rc=cfg_mutate(&client,CFG_CONNECT,NULL,0);
                    if (!rc) rc=cfg_wait_link(&client);
                    result(rc); status();
                } else if (equal(line,(unsigned)n,"DISCONNECT")) result(cfg_mutate(&client,CFG_DISCONNECT,NULL,0));
                else if (equal(line,(unsigned)n,"SAVE")) {
                    if (confirm()) result(cfg_mutate(&client,CFG_SAVE,NULL,0));
                    else put("Cancelled.\n");
                } else if (equal(line,(unsigned)n,"ERASE")) {
                    if (confirm()) result(cfg_mutate(&client,CFG_ERASE,NULL,0));
                    else put("Cancelled.\n");
                } else if (equal(line,(unsigned)n,"REBOOT")) {
                    cfg_sim_reboot(); put("SIM adapter restarted.\n"); status();
                } else put("Unknown command. Type help.\n");
            }
            cfg_sim_close(); cfg_os_close(console); console=0;
        }
    } else { put("Usage: FunkOttoConfig SELFTEST | SIM\nParallel transport is not implemented.\n"); exit_code=10; }
    fo_wipe(line,sizeof(line)); fo_wipe(profile,sizeof(profile)); fo_wipe(&client,sizeof(client));
    if (cancelled) exit_code=5;
    if (io_failed) exit_code=20;
    os_close_library(DOSBase); DOSBase=NULL; return exit_code;
}
