#include "tools/tools.h"
#include "tools/mcp_client.h"
#include "util/json_util.h"
#include <stdlib.h>
#include <string.h>

char *tool_execute_list_mcp_resources(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *server = json_get_string(json, "server");
    if (!server || !server[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'server' argument required");
    }
    char *result = mcp_list_resources(cfg, server);
    cJSON_Delete(json);
    return result;
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
    char *result = mcp_read_resource(cfg, server, uri);
    cJSON_Delete(json);
    return result;
}
