#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_config_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;
    GooseConfig *mutable_cfg = (GooseConfig *)cfg;
    StrBuf out = strbuf_new();

    if (!args || !args[0]) {
        strbuf_append(&out, "Current configuration:\n");
        strbuf_append_fmt(&out, "  provider: %s\n", cfg->provider ? cfg->provider : provider_profile_detect(cfg)->name);
        strbuf_append_fmt(&out, "  model: %s\n", cfg->model);
        strbuf_append_fmt(&out, "  base_url: %s\n", cfg->base_url);
        strbuf_append_fmt(&out, "  output_style: %s\n", (cfg->output_style && cfg->output_style[0]) ? cfg->output_style : "(default)");
        strbuf_append_fmt(&out, "  api_key: %s\n", (cfg->api_key && cfg->api_key[0]) ? "configured" : "not configured");
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

    if (strcmp(first, "provider") == 0) {
        if (rest && rest[0]) {
            if (provider_apply_preset(mutable_cfg, rest, 0) != 0) {
                strbuf_append(&out, "Unknown provider preset. Use /provider list\n");
            } else {
                config_save_user_settings(mutable_cfg);
                strbuf_append_fmt(&out, "Provider set to: %s\n", mutable_cfg->provider);
                strbuf_append_fmt(&out, "Base URL: %s\n", mutable_cfg->base_url);
            }
        } else strbuf_append_fmt(&out, "Current provider: %s\n", cfg->provider ? cfg->provider : provider_profile_detect(cfg)->name);
    } else if (strcmp(first, "model") == 0) {
        if (rest && rest[0]) {
            free(mutable_cfg->model);
            mutable_cfg->model = strdup(rest);
            config_save_user_settings(mutable_cfg);
            strbuf_append_fmt(&out, "Model set to: %s\n", rest);
        }
        else strbuf_append_fmt(&out, "Current model: %s\n", cfg->model);
    } else if (strcmp(first, "base_url") == 0) {
        if (rest && rest[0]) {
            free(mutable_cfg->base_url);
            mutable_cfg->base_url = strdup(rest);
            config_save_user_settings(mutable_cfg);
            strbuf_append_fmt(&out, "Base URL set to: %s\n", rest);
        }
        else strbuf_append_fmt(&out, "Current base URL: %s\n", cfg->base_url);
    } else if (strcmp(first, "output_style") == 0) {
        if (rest && rest[0]) {
            free(mutable_cfg->output_style);
            mutable_cfg->output_style = strdup(rest);
            config_save_user_settings(mutable_cfg);
            strbuf_append_fmt(&out, "Output style set to: %s\n", rest);
        } else {
            strbuf_append_fmt(&out, "Current output style: %s\n", (cfg->output_style && cfg->output_style[0]) ? cfg->output_style : "(default)");
        }
    } else if (strcmp(first, "api_key") == 0) {
        if (rest && rest[0]) {
            free(mutable_cfg->api_key);
            mutable_cfg->api_key = strdup(rest);
            config_save_user_settings(mutable_cfg);
            strbuf_append(&out, "API key updated and saved.\n");
        } else {
            strbuf_append_fmt(&out, "API key: %s\n", (cfg->api_key && cfg->api_key[0]) ? "configured" : "not configured");
        }
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
        strbuf_append(&out, "Supported: provider, model, base_url, output_style, api_key, permission_mode, max_tokens, max_turns, working_dir\n");
    }

    free(work);
    return strbuf_detach(&out);
}

void cmd_config_register(CommandRegistry *reg) {
    Command cmd = {strdup("config"), strdup("Show or inspect runtime configuration"), 1, cmd_config_exec};
    command_registry_register(reg, cmd);
}
