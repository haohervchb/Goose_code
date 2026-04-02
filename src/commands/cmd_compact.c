#include "commands/commands.h"
#include "api.h"
#include "compact.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_compact_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    int keep = 10;
    char *summary = NULL;

    ApiConfig api_cfg = {
        .base_url = cfg->base_url,
        .api_key = cfg->api_key,
        .model = cfg->model,
        .max_tokens = cfg->max_tokens,
        .temperature = cfg->temperature,
        .max_retries = 2,
    };

    if (args && strcmp(args, "full") == 0) {
        summary = compact_generate_summary(&api_cfg, sess->messages);
        if (summary) session_apply_compact_summary(sess, 0, summary);
    } else {
        int total = cJSON_GetArraySize(sess->messages);
        int compact_to = total - keep;
        if (compact_to > 1) {
            cJSON *prefix_messages = cJSON_CreateArray();
            for (int i = 0; i < compact_to && i < total; i++) {
                cJSON *item = cJSON_GetArrayItem(sess->messages, i);
                if (item) cJSON_AddItemToArray(prefix_messages, cJSON_Duplicate(item, 1));
            }
            summary = compact_generate_partial_summary(&api_cfg, prefix_messages, COMPACT_PARTIAL_UP_TO);
            cJSON_Delete(prefix_messages);
            if (summary) session_apply_compact_summary(sess, keep, summary);
        }
    }

    if (!summary) summary = session_compact(sess, keep);
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
