#ifndef FO_PROFILE_STORE_H
#define FO_PROFILE_STORE_H
#include "funkotto/wifi_model.h"
#define FO_STORE_OFFSET 0x003fe000u
#define FO_STORE_SECTOR 4096u
#define FO_STORE_PAGE 256u
enum fo_store_state { STORE_EMPTY, STORE_PROFILE, STORE_DELETED, STORE_CORRUPT, STORE_INCOMPATIBLE, STORE_AMBIGUOUS };
enum fo_store_error { STORE_OK, STORE_IO, STORE_VERIFY, STORE_FORMAT, STORE_NOT_READY };
struct fo_store_io {
    const uint8_t *(*read)(unsigned slot);
    bool (*erase)(unsigned slot);
    bool (*program)(unsigned slot, unsigned page, const uint8_t *data);
};
struct fo_store {
    const struct fo_store_io *io;
    enum fo_store_state state; enum fo_store_error error;
    int active; uint32_t sequence; struct fo_profile profile;
    /* Private writer scratch in SRAM. Never put credentials in public status. */
    _Alignas(4) uint8_t page[FO_STORE_PAGE], commit[FO_STORE_PAGE];
};
void fo_store_load(struct fo_store *s, const struct fo_store_io *io);
bool fo_store_save(struct fo_store *s, const struct fo_profile *profile);
bool fo_store_erase(struct fo_store *s);
bool fo_store_cleanup(struct fo_store *s);
bool fo_store_matches(const struct fo_store *s, const struct fo_profile *p);
const char *fo_store_state_name(enum fo_store_state state);
/* Pico backend: initialize victim on Core 0, open only after WLAN deinit on Core 1. */
bool fo_flash_core_init(void);
bool fo_flash_open(void);
void fo_flash_close(void);
extern const struct fo_store_io fo_flash_io;
#endif
