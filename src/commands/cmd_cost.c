#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>

static char *cmd_cost_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg;
    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Token usage for session %s:\n", sess->id);
    strbuf_append_fmt(&out, "  Input tokens:  %ld\n", sess->total_input_tokens);
    strbuf_append_fmt(&out, "  Output tokens: %ld\n", sess->total_output_tokens);
    strbuf_append_fmt(&out, "  Total tokens:  %ld\n", sess->total_input_tokens + sess->total_output_tokens);
    strbuf_append_fmt(&out, "  Turns: %d\n", sess->turn_count);
    return strbuf_detach(&out);
}

void cmd_cost_register(CommandRegistry *reg) {
    Command cmd = {strdup("cost"), strdup("Show token usage"), 0, cmd_cost_exec};
    command_registry_register(reg, cmd);
}
