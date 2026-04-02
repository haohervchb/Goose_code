#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_model_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;
    GooseConfig *mutable_cfg = (GooseConfig *)cfg;
    StrBuf out = strbuf_new();
    if (args && strncmp(args, "list", 4) == 0 && (args[4] == '\0' || args[4] == ' ')) {
        strbuf_free(&out);
        return provider_list_models(cfg);
    }
    if (args && strncmp(args, "set ", 4) == 0) {
        args += 4;
    }
    if (args && strlen(args) > 0) {
        char *old = mutable_cfg->model ? strdup(mutable_cfg->model) : strdup("(none)");
        free(mutable_cfg->model);
        mutable_cfg->model = strdup(args);
        config_save_user_settings(mutable_cfg);
        strbuf_append_fmt(&out, "Model changed to: %s\n", mutable_cfg->model);
        strbuf_append_fmt(&out, "Previous model was: %s\n", old);
        free(old);
    } else {
        const ProviderProfile *profile = provider_profile_detect(cfg);
        strbuf_append_fmt(&out, "Current model: %s\n", cfg->model);
        strbuf_append_fmt(&out, "Provider: %s\n", profile ? profile->name : "unknown");
        strbuf_append(&out, "Usage: /model <model_name>\n");
        strbuf_append(&out, "       /model set <model_name>\n");
        strbuf_append(&out, "       /model list\n");
    }
    return strbuf_detach(&out);
}

void cmd_model_register(CommandRegistry *reg) {
    Command cmd = {strdup("model"), strdup("Show or set the model"), 1, cmd_model_exec};
    command_registry_register(reg, cmd);
}
