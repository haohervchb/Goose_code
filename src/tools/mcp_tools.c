#include "tools/tools.h"
#include "tools/mcp_client.h"
#include "util/json_util.h"
#include <stdlib.h>
#include <string.h>

static char *mcp_wrap_result(const char *server, const char *payload_key, char *raw) {
    if (!raw) raw = strdup("Error: MCP tool returned no result");

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", strstr(raw, "Error:") != raw);
    cJSON_AddStringToObject(result, "server", server);

    if (strstr(raw, "Error:") == raw) {
        cJSON_AddStringToObject(result, "error", raw + 7);
        free(raw);
        char *out = json_to_string(result);
        cJSON_Delete(result);
        return out ? out : strdup("{\"ok\":false,\"error\":\"MCP error\"}");
    }

    cJSON *payload = cJSON_Parse(raw);
    free(raw);
    if (!payload) {
        cJSON_AddBoolToObject(result, "ok", 0);
        cJSON_AddStringToObject(result, "error", "Failed to parse MCP payload");
    } else {
        cJSON_AddItemToObject(result, payload_key, payload);
    }

    char *out = json_to_string(result);
    cJSON_Delete(result);
    return out ? out : strdup("{\"ok\":false,\"error\":\"MCP encoding error\"}");
}

char *tool_execute_list_mcp_resources(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *server = json_get_string(json, "server");
    if (!server || !server[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'server' argument required");
    }
    char *server_copy = strdup(server);
    char *result = mcp_list_resources(cfg, server);
    cJSON_Delete(json);
    char *wrapped = mcp_wrap_result(server_copy, "resources", result);
    free(server_copy);
    return wrapped;
}

char *tool_execute_read_mcp_resource(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *server = json_get_string(json, "server");
    const char *uri = json_get_string(json, "uri");
    if (!server || !server[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'server' argument required");
    }
    if (!uri || !uri[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'uri' argument required");
    }
    char *server_copy = strdup(server);
    char *result = mcp_read_resource(cfg, server, uri);
    cJSON_Delete(json);
    char *wrapped = mcp_wrap_result(server_copy, "contents", result);
    free(server_copy);
    return wrapped;
}
