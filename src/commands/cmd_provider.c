#include "commands/commands.h"
#include "util/terminal.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_provider_value(const char *label, const char *default_value) {
    StrBuf prompt = strbuf_new();
    strbuf_append(&prompt, label);
    if (default_value && default_value[0]) {
        strbuf_append_fmt(&prompt, " [%s]", default_value);
    }
    strbuf_append(&prompt, ": ");
    char *value = term_read_line_opts(prompt.data, 0, 0);
    strbuf_free(&prompt);
    if (!value) return default_value ? strdup(default_value) : strdup("");
    if (value[0] == '\0' && default_value) {
        free(value);
        return strdup(default_value);
    }
    return value;
}

static char *cmd_provider_show(GooseConfig *cfg) {
    const ProviderProfile *profile = provider_profile_detect(cfg);
    StrBuf out = strbuf_from("Current provider:\n");
    strbuf_append_fmt(&out, "  provider: %s\n", profile ? profile->name : "unknown");
    if (profile) strbuf_append_fmt(&out, "  display: %s\n", profile->display_name);
    strbuf_append_fmt(&out, "  base_url: %s\n", cfg->base_url ? cfg->base_url : "(none)");
    strbuf_append_fmt(&out, "  model: %s\n", cfg->model ? cfg->model : "(none)");
    strbuf_append_fmt(&out, "  auth: %s\n", provider_requires_api_key(cfg) ? "api_key required" : "no api_key required");
    strbuf_append_fmt(&out, "  api_key: %s\n", (cfg->api_key && cfg->api_key[0]) ? "configured" : "not configured");
    return strbuf_detach(&out);
}

static char *cmd_provider_list(void) {
    StrBuf out = strbuf_from("Available provider presets:\n");
    for (size_t i = 0; i < provider_profile_count(); i++) {
        const ProviderProfile *p = provider_profile_at(i);
        strbuf_append_fmt(&out, "- %s (%s) -> %s\n", p->name, p->display_name, p->default_base_url);
    }
    return strbuf_detach(&out);
}

static char *cmd_provider_set(GooseConfig *cfg, const char *name) {
    if (provider_apply_preset(cfg, name, 1) != 0) {
        return strdup("Unknown provider preset. Use /provider list\n");
    }

    char *saved_base = NULL;
    char *saved_model = NULL;
    char *saved_key = NULL;
    config_load_user_provider_settings(cfg->provider, &saved_base, &saved_model, &saved_key);

    const ProviderProfile *profile = provider_profile_detect(cfg);
    char *base_url = read_provider_value("Base URL", saved_base ? saved_base : cfg->base_url);
    char *model = read_provider_value("Model", saved_model ? saved_model : cfg->model);
    char *api_key = NULL;
    if (provider_requires_api_key(cfg)) {
        api_key = read_provider_value("API key", saved_key ? saved_key : cfg->api_key);
    } else {
        api_key = read_provider_value("API key (optional)", saved_key ? saved_key : (cfg->api_key ? cfg->api_key : ""));
    }

    free(cfg->base_url);
    cfg->base_url = base_url;
    free(cfg->model);
    cfg->model = model;
    free(cfg->api_key);
    cfg->api_key = (api_key && api_key[0]) ? api_key : NULL;
    if (!cfg->api_key) free(api_key);

    config_save_user_settings(cfg);
    StrBuf out = strbuf_from("Provider updated:\n");
    strbuf_append_fmt(&out, "  provider: %s\n", profile->name);
    strbuf_append_fmt(&out, "  base_url: %s\n", cfg->base_url);
    strbuf_append_fmt(&out, "  model: %s\n", cfg->model);
    if (provider_requires_api_key(cfg) && (!cfg->api_key || !cfg->api_key[0])) {
        strbuf_append(&out, "  note: this provider requires OPENAI_API_KEY or /config api_key <key>\n");
    }
    free(saved_base);
    free(saved_model);
    free(saved_key);
    return strbuf_detach(&out);
}

static char *cmd_provider_exec(const char *args, const GooseConfig *cfg_in, Session *sess) {
    (void)sess;
    GooseConfig *cfg = (GooseConfig *)cfg_in;

    if (!args || !args[0] || strcmp(args, "show") == 0) return cmd_provider_show(cfg);
    if (strcmp(args, "list") == 0) return cmd_provider_list();
    if (strcmp(args, "test") == 0) return provider_test_connection(cfg);
    if (strncmp(args, "set ", 4) == 0) return cmd_provider_set(cfg, args + 4);

    return strdup(
        "Usage:\n"
        "/provider\n"
        "/provider show\n"
        "/provider list\n"
        "/provider set <openai|ollama|vllm|llama.cpp|ik-llama>\n"
        "/provider test\n");
}

void cmd_provider_register(CommandRegistry *reg) {
    Command cmd = {strdup("provider"), strdup("Show, set, and test provider presets"), 1, cmd_provider_exec};
    command_registry_register(reg, cmd);
}
