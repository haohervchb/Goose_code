#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_compact_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg;
    int keep = 10;
    char *summary = session_compact(sess, keep);
    StrBuf out = strbuf_new();
    if (summary) {
        strbuf_append_fmt(&out, "Context compacted. Summary: %.200s...\n", summary);
        strbuf_append_fmt(&out, "Messages remaining: %d\n", cJSON_GetArraySize(sess->messages));
        free(summary);
    } else {
        strbuf_append(&out, "Context is small enough, no compaction needed.\n");
    }
    return strbuf_detach(&out);
}

void cmd_compact_register(CommandRegistry *reg) {
    Command cmd = {strdup("compact"), strdup("Compact conversation context"), 0, cmd_compact_exec};
    command_registry_register(reg, cmd);
}
