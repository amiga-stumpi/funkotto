#ifndef FUNKOTTO_CONSOLE_H
#define FUNKOTTO_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FO_CONSOLE_CAPACITY 48u
enum fo_command { FO_CMD_NONE, FO_CMD_HELP, FO_CMD_INFO, FO_CMD_STATUS, FO_CMD_INVALID };
struct fo_console { char line[FO_CONSOLE_CAPACITY]; size_t length; bool invalid; };
void fo_console_reset(struct fo_console *console);
enum fo_command fo_console_feed(struct fo_console *console, uint8_t byte);
#endif
