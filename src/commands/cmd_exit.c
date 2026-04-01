#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_exit_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args;
    session_save(cfg->session_dir, sess);
    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Session saved: %s\n", sess->id);
    strbuf_append(&out, "Goodbye!\n");
    return strbuf_detach(&out);
}

void cmd_exit_register(CommandRegistry *reg) {
    Command cmd = {strdup("exit"), strdup("Exit the REPL"), 0, cmd_exit_exec};
    command_registry_register(reg, cmd);
}
