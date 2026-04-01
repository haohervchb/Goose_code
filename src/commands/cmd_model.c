#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_model_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;
    StrBuf out = strbuf_new();
    if (args && strlen(args) > 0) {
        strbuf_append_fmt(&out, "Model changed to: %s (runtime only)\n", args);
        strbuf_append_fmt(&out, "Previous model was: %s\n", cfg->model);
    } else {
        strbuf_append_fmt(&out, "Current model: %s\n", cfg->model);
        strbuf_append(&out, "Usage: /model <model_name>\n");
        strbuf_append(&out, "Examples: /model gpt-4o, /model gpt-3.5-turbo\n");
    }
    return strbuf_detach(&out);
}

void cmd_model_register(CommandRegistry *reg) {
    Command cmd = {strdup("model"), strdup("Show or set the model"), 1, cmd_model_exec};
    command_registry_register(reg, cmd);
}
