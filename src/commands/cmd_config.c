#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_config_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;
    StrBuf out = strbuf_new();

    if (!args || !args[0]) {
        strbuf_append(&out, "Current configuration:\n");
        strbuf_append_fmt(&out, "  model: %s\n", cfg->model);
        strbuf_append_fmt(&out, "  base_url: %s\n", cfg->base_url);
        strbuf_append_fmt(&out, "  permission_mode: %s\n", config_perm_mode_str(cfg->permission_mode));
        strbuf_append_fmt(&out, "  max_tokens: %d\n", cfg->max_tokens);
        strbuf_append_fmt(&out, "  max_turns: %d\n", cfg->max_turns);
        strbuf_append_fmt(&out, "  working_dir: %s\n", cfg->working_dir);
        return strbuf_detach(&out);
    }

    char *work = strdup(args);
    char *first = strtok(work, " ");
    char *rest = strtok(NULL, "");

    if (!first) {
        free(work);
        return strbuf_detach(&out);
    }

    if (strcmp(first, "model") == 0) {
        if (rest && rest[0]) strbuf_append_fmt(&out, "Model set to: %s (runtime only)\n", rest);
        else strbuf_append_fmt(&out, "Current model: %s\n", cfg->model);
    } else if (strcmp(first, "base_url") == 0) {
        if (rest && rest[0]) strbuf_append_fmt(&out, "Base URL set to: %s (runtime only)\n", rest);
        else strbuf_append_fmt(&out, "Current base URL: %s\n", cfg->base_url);
    } else if (strcmp(first, "permission_mode") == 0) {
        if (rest && rest[0]) strbuf_append_fmt(&out, "Permission mode set to: %s (runtime only)\n", rest);
        else strbuf_append_fmt(&out, "Current permission mode: %s\n", config_perm_mode_str(cfg->permission_mode));
    } else if (strcmp(first, "max_tokens") == 0) {
        if (rest && rest[0]) strbuf_append_fmt(&out, "Max tokens set to: %s (runtime only)\n", rest);
        else strbuf_append_fmt(&out, "Current max tokens: %d\n", cfg->max_tokens);
    } else if (strcmp(first, "max_turns") == 0) {
        if (rest && rest[0]) strbuf_append_fmt(&out, "Max turns set to: %s (runtime only)\n", rest);
        else strbuf_append_fmt(&out, "Current max turns: %d\n", cfg->max_turns);
    } else if (strcmp(first, "working_dir") == 0) {
        if (rest && rest[0]) strbuf_append(&out, "Working directory cannot be changed via /config\n");
        else strbuf_append_fmt(&out, "Current working directory: %s\n", cfg->working_dir);
    } else {
        strbuf_append_fmt(&out, "Unknown setting: %s\n", first);
        strbuf_append(&out, "Supported: model, base_url, permission_mode, max_tokens, max_turns, working_dir\n");
    }

    free(work);
    return strbuf_detach(&out);
}

void cmd_config_register(CommandRegistry *reg) {
    Command cmd = {strdup("config"), strdup("Show or inspect runtime configuration"), 1, cmd_config_exec};
    command_registry_register(reg, cmd);
}
