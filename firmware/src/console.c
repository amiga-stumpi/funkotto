#include "funkotto/console.h"
#include <string.h>

void fo_console_reset(struct fo_console *c) {
    c->length = 0;
    c->invalid = false;
}

enum fo_command fo_console_feed(struct fo_console *c, uint8_t byte) {
    if (byte == '\r' || byte == '\n') {
        enum fo_command command = FO_CMD_INVALID;
        c->line[c->length] = '\0';
        if (!c->invalid) {
            if (c->length == 0) command = FO_CMD_NONE;
            else if (strcmp(c->line, "help") == 0) command = FO_CMD_HELP;
            else if (strcmp(c->line, "info") == 0) command = FO_CMD_INFO;
            else if (strcmp(c->line, "status") == 0) command = FO_CMD_STATUS;
        }
        fo_console_reset(c);
        return command;
    }
    if (byte < 32u || byte > 126u || c->length >= FO_CONSOLE_CAPACITY - 1u)
        c->invalid = true;
    if (!c->invalid) c->line[c->length++] = (char)byte;
    return FO_CMD_NONE;
}
