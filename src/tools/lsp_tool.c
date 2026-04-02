#include "tools/tools.h"
#include "tools/lsp_client.h"
#include "util/json_util.h"
#include <stdlib.h>
#include <string.h>

char *tool_execute_lsp(const char *args, const GooseConfig *cfg) {
    cJSON *input = cJSON_Parse(args);
    const char *action = input ? json_get_string(input, "action") : NULL;
    char *raw = lsp_execute_request(cfg, args);

    cJSON *result = cJSON_CreateObject();
    if (action) cJSON_AddStringToObject(result, "action", action);

    if (!raw) raw = strdup("Error: LSP tool returned no result");
    if (strstr(raw, "Error:") == raw) {
        cJSON_AddBoolToObject(result, "ok", 0);
        cJSON_AddStringToObject(result, "error", raw + 7);
        free(raw);
        if (input) cJSON_Delete(input);
        char *out = json_to_string(result);
        cJSON_Delete(result);
        return out ? out : strdup("{\"ok\":false,\"error\":\"LSP error\"}");
    }

    cJSON *payload = cJSON_Parse(raw);
    free(raw);
    if (!payload) {
        cJSON_AddBoolToObject(result, "ok", 0);
        cJSON_AddStringToObject(result, "error", "Failed to parse LSP payload");
    } else {
        cJSON *ok = cJSON_GetObjectItem(payload, "ok");
        if (!ok) cJSON_AddBoolToObject(payload, "ok", 1);
        cJSON_Delete(result);
        result = payload;
    }

    if (input) cJSON_Delete(input);
    char *out = json_to_string(result);
    cJSON_Delete(result);
    return out ? out : strdup("{\"ok\":false,\"error\":\"LSP encoding error\"}");
}
