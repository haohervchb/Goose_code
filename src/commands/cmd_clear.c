#include "commands/commands.h"
#include "util/terminal.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_clear_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg; (void)sess;
    term_clear_screen();
    return strdup("");
}

void cmd_clear_register(CommandRegistry *reg) {
    Command cmd = {strdup("clear"), strdup("Clear the screen"), 0, cmd_clear_exec};
    command_registry_register(reg, cmd);
}
