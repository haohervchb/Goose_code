#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_session_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    StrBuf out = strbuf_new();

    if (!args || strlen(args) == 0) {
        strbuf_append_fmt(&out, "Current session: %s\n", sess->id);
        strbuf_append_fmt(&out, "Messages: %d\n", cJSON_GetArraySize(sess->messages));
        strbuf_append_fmt(&out, "Turns: %d\n", sess->turn_count);
        strbuf_append_fmt(&out, "Input tokens: %ld\n", sess->total_input_tokens);
        strbuf_append_fmt(&out, "Output tokens: %ld\n", sess->total_output_tokens);

        session_save(cfg->session_dir, sess);
        strbuf_append_fmt(&out, "Session saved to: %s/%s.json\n", cfg->session_dir, sess->id);

        char *list = session_list(cfg->session_dir);
        strbuf_append(&out, list);
        free(list);
    } else if (strcmp(args, "list") == 0) {
        char *list = session_list(cfg->session_dir);
        strbuf_append(&out, list);
        free(list);
    } else {
        Session *loaded = session_load(cfg->session_dir, args);
        if (loaded) {
            strbuf_append_fmt(&out, "Loaded session: %s\n", loaded->id);
            strbuf_append_fmt(&out, "Messages: %d\n", cJSON_GetArraySize(loaded->messages));
            session_free(loaded);
        } else {
            strbuf_append_fmt(&out, "Session not found: %s\n", args);
        }
    }
    return strbuf_detach(&out);
}

void cmd_session_register(CommandRegistry *reg) {
    Command cmd = {strdup("session"), strdup("Show, save, or load a session"), 1, cmd_session_exec};
    command_registry_register(reg, cmd);
}
