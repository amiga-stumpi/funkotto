#ifndef FO_AMIGA_OS_H
#define FO_AMIGA_OS_H
#include <stdint.h>
extern void *SysBase,*DOSBase,*MiscBase;
void *os_open_library(const char *,uint32_t);
void os_close_library(void *);
void *os_open_resource(const char *);
void os_disable(void);
void os_enable(void);
uint32_t os_signals(void);
void *os_alloc_misc(uint32_t,const char *);
void os_free_misc(uint32_t);
uint32_t os_output(void);
int32_t os_write(uint32_t,const void *,uint32_t);
#endif
