#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_config_tool(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *setting = json_get_string(json, "setting");
    const char *value = json_get_string(json, "value");

    StrBuf out = strbuf_new();

    if (!setting) {
        strbuf_append(&out, "Current configuration:\n");
        strbuf_append_fmt(&out, "  model: %s\n", cfg->model);
        strbuf_append_fmt(&out, "  base_url: %s\n", cfg->base_url);
        strbuf_append_fmt(&out, "  permission_mode: %s\n", config_perm_mode_str(cfg->permission_mode));
        strbuf_append_fmt(&out, "  max_tokens: %d\n", cfg->max_tokens);
        strbuf_append_fmt(&out, "  max_turns: %d\n", cfg->max_turns);
        strbuf_append_fmt(&out, "  working_dir: %s\n", cfg->working_dir);
        char *result = strbuf_detach(&out);
        cJSON_Delete(json);
        return result;
    }

    if (strcmp(setting, "model") == 0) {
        if (value) {
            strbuf_append_fmt(&out, "Model set to: %s (runtime only)", value);
        } else {
            strbuf_append_fmt(&out, "Current model: %s", cfg->model);
        }
    } else if (strcmp(setting, "permission_mode") == 0) {
        if (value) {
            strbuf_append_fmt(&out, "Permission mode set to: %s (runtime only)", value);
        } else {
            strbuf_append_fmt(&out, "Current permission mode: %s", config_perm_mode_str(cfg->permission_mode));
        }
    } else if (strcmp(setting, "max_tokens") == 0) {
        if (value) {
            strbuf_append_fmt(&out, "Max tokens set to: %s (runtime only)", value);
        } else {
            strbuf_append_fmt(&out, "Current max tokens: %d", cfg->max_tokens);
        }
    } else {
        strbuf_append_fmt(&out, "Unknown setting: %s. Supported: model, permission_mode, max_tokens", setting);
    }

    char *result = strbuf_detach(&out);
    cJSON_Delete(json);
    return result;
}
