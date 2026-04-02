#include "system_init.h"
#include "util/json_util.h"

cJSON *system_init_build_metadata(const Agent *agent) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "cwd", agent->config.working_dir ? agent->config.working_dir : ".");
    cJSON_AddStringToObject(json, "provider", agent->config.provider ? agent->config.provider : "unknown");
    cJSON_AddStringToObject(json, "model", agent->config.model ? agent->config.model : "");
    cJSON_AddStringToObject(json, "base_url", agent->config.base_url ? agent->config.base_url : "");
    cJSON_AddStringToObject(json, "permission_mode", config_perm_mode_str(agent->config.permission_mode));
    cJSON_AddStringToObject(json, "session_id", agent->session && agent->session->id ? agent->session->id : "");

    cJSON *tools = cJSON_CreateArray();
    for (int i = 0; i < agent->tools.count; i++) {
        cJSON_AddItemToArray(tools, cJSON_CreateString(agent->tools.tools[i]->name));
    }
    cJSON_AddItemToObject(json, "tools", tools);

    cJSON *commands = cJSON_CreateArray();
    for (int i = 0; i < agent->commands.count; i++) {
        cJSON_AddItemToArray(commands, cJSON_CreateString(agent->commands.commands[i]->name));
    }
    cJSON_AddItemToObject(json, "commands", commands);

    cJSON *subagents = cJSON_CreateArray();
    cJSON_AddItemToArray(subagents, cJSON_CreateString("general"));
    cJSON_AddItemToArray(subagents, cJSON_CreateString("explore"));
    cJSON_AddItemToArray(subagents, cJSON_CreateString("plan"));
    cJSON_AddItemToObject(json, "subagent_types", subagents);

    cJSON_AddItemToObject(json, "mcp_servers", agent->config.mcp_servers ? cJSON_Duplicate(agent->config.mcp_servers, 1) : cJSON_CreateArray());
    return json;
}

char *system_init_render_metadata(const Agent *agent) {
    cJSON *json = system_init_build_metadata(agent);
    char *out = json_to_string(json);
    cJSON_Delete(json);
    return out;
}
