#include "tools/tools.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ToolRegistry tool_registry_init(void) {
    ToolRegistry reg = {0};
    reg.cap = 32;
    reg.tools = calloc(reg.cap, sizeof(Tool *));
    return reg;
}

void tool_registry_register(ToolRegistry *reg, Tool tool) {
    if (reg->count >= reg->cap) {
        reg->cap *= 2;
        reg->tools = realloc(reg->tools, reg->cap * sizeof(Tool *));
    }
    reg->tools[reg->count] = malloc(sizeof(Tool));
    *reg->tools[reg->count] = tool;
    reg->count++;
}

void tool_registry_free(ToolRegistry *reg) {
    for (int i = 0; i < reg->count; i++) {
        if (reg->tools[i]) {
            free(reg->tools[i]->name);
            free(reg->tools[i]->description);
            if (reg->tools[i]->parameters_schema) cJSON_Delete(reg->tools[i]->parameters_schema);
            free(reg->tools[i]);
        }
    }
    free(reg->tools);
    reg->tools = NULL;
    reg->count = 0;
    reg->cap = 0;
}

cJSON *tool_registry_get_definitions(const ToolRegistry *reg) {
    cJSON *defs = cJSON_CreateArray();
    for (int i = 0; i < reg->count; i++) {
        Tool *t = reg->tools[i];
        cJSON *def = json_build_tool_def_openai(t->name, t->description,
                                                  t->parameters_schema ? cJSON_Duplicate(t->parameters_schema, 1) : NULL);
        cJSON_AddItemToArray(defs, def);
    }
    return defs;
}

Tool *tool_registry_find(const ToolRegistry *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i]->name, name) == 0) return reg->tools[i];
    }
    return NULL;
}

char *tool_registry_execute(ToolRegistry *reg, const char *name, const char *args,
                            const GooseConfig *cfg, PermissionCheckResult *perm) {
    Tool *tool = tool_registry_find(reg, name);
    if (!tool) return strdup("Error: tool not found");

    *perm = permissions_check(cfg, name, args, tool->required_mode);
    if (*perm == PERM_CHECK_BLOCK) return strdup("Error: tool is blocked by permission policy");
    if (*perm == PERM_CHECK_DENY) return strdup("Error: tool is denied");

    if (!args) return strdup("Error: missing tool arguments");

    return tool->execute(args, cfg);
}

void tool_registry_register_all(ToolRegistry *reg) {
    cJSON *bash_params = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_params, "type", "object");
    cJSON *bash_props = cJSON_CreateObject();
    cJSON *bash_cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_cmd, "type", "string");
    cJSON_AddStringToObject(bash_cmd, "description", "The bash command to execute");
    cJSON_AddItemToObject(bash_props, "command", bash_cmd);
    cJSON_AddItemToObject(bash_params, "properties", bash_props);
    cJSON *bash_req = cJSON_CreateArray();
    cJSON_AddItemToArray(bash_req, cJSON_CreateString("command"));
    cJSON_AddItemToObject(bash_params, "required", bash_req);
    Tool bash = {
        .name = strdup("bash"),
        .description = strdup("Execute a bash command in the current directory. Returns stdout and stderr."),
        .parameters_schema = bash_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_bash
    };
    tool_registry_register(reg, bash);

    cJSON *read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(read_params, "type", "object");
    cJSON *read_props = cJSON_CreateObject();
    cJSON *read_path = cJSON_CreateObject();
    cJSON_AddStringToObject(read_path, "type", "string");
    cJSON_AddStringToObject(read_path, "description", "Path to the file to read");
    cJSON_AddItemToObject(read_props, "file_path", read_path);
    cJSON_AddItemToObject(read_params, "properties", read_props);
    cJSON *read_req = cJSON_CreateArray();
    cJSON_AddItemToArray(read_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(read_params, "required", read_req);
    Tool read_file = {
        .name = strdup("read_file"),
        .description = strdup("Read the contents of a file."),
        .parameters_schema = read_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_read_file
    };
    tool_registry_register(reg, read_file);

    cJSON *write_params = cJSON_CreateObject();
    cJSON_AddStringToObject(write_params, "type", "object");
    cJSON *write_props = cJSON_CreateObject();
    cJSON *write_path = cJSON_CreateObject();
    cJSON_AddStringToObject(write_path, "type", "string");
    cJSON_AddStringToObject(write_path, "description", "Path to the file to write");
    cJSON_AddItemToObject(write_props, "file_path", write_path);
    cJSON *write_content = cJSON_CreateObject();
    cJSON_AddStringToObject(write_content, "type", "string");
    cJSON_AddStringToObject(write_content, "description", "Content to write to the file");
    cJSON_AddItemToObject(write_props, "content", write_content);
    cJSON_AddItemToObject(write_params, "properties", write_props);
    cJSON *write_req = cJSON_CreateArray();
    cJSON_AddItemToArray(write_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToArray(write_req, cJSON_CreateString("content"));
    cJSON_AddItemToObject(write_params, "required", write_req);
    Tool write_file = {
        .name = strdup("write_file"),
        .description = strdup("Write content to a file, creating it if it doesn't exist."),
        .parameters_schema = write_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_write_file
    };
    tool_registry_register(reg, write_file);

    cJSON *edit_params = cJSON_CreateObject();
    cJSON_AddStringToObject(edit_params, "type", "object");
    cJSON *edit_props = cJSON_CreateObject();
    cJSON *edit_path = cJSON_CreateObject();
    cJSON_AddStringToObject(edit_path, "type", "string");
    cJSON_AddStringToObject(edit_path, "description", "Path to the file to edit");
    cJSON_AddItemToObject(edit_props, "file_path", edit_path);
    cJSON *edit_old = cJSON_CreateObject();
    cJSON_AddStringToObject(edit_old, "type", "string");
    cJSON_AddStringToObject(edit_old, "description", "The text to replace");
    cJSON_AddItemToObject(edit_props, "old_string", edit_old);
    cJSON *edit_new = cJSON_CreateObject();
    cJSON_AddStringToObject(edit_new, "type", "string");
    cJSON_AddStringToObject(edit_new, "description", "The text to replace it with");
    cJSON_AddItemToObject(edit_props, "new_string", edit_new);
    cJSON_AddItemToObject(edit_params, "properties", edit_props);
    cJSON *edit_req = cJSON_CreateArray();
    cJSON_AddItemToArray(edit_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToArray(edit_req, cJSON_CreateString("old_string"));
    cJSON_AddItemToArray(edit_req, cJSON_CreateString("new_string"));
    cJSON_AddItemToObject(edit_params, "required", edit_req);
    Tool edit_file = {
        .name = strdup("edit_file"),
        .description = strdup("Edit a file by replacing a specific string with another string."),
        .parameters_schema = edit_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_edit_file
    };
    tool_registry_register(reg, edit_file);

    cJSON *glob_params = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_params, "type", "object");
    cJSON *glob_props = cJSON_CreateObject();
    cJSON *glob_pat = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_pat, "type", "string");
    cJSON_AddStringToObject(glob_pat, "description", "The glob pattern to match");
    cJSON_AddItemToObject(glob_props, "pattern", glob_pat);
    cJSON_AddItemToObject(glob_params, "properties", glob_props);
    cJSON *glob_req = cJSON_CreateArray();
    cJSON_AddItemToArray(glob_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(glob_params, "required", glob_req);
    Tool glob_search = {
        .name = strdup("glob_search"),
        .description = strdup("Find files matching a glob pattern."),
        .parameters_schema = glob_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_glob_search
    };
    tool_registry_register(reg, glob_search);

    cJSON *grep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_params, "type", "object");
    cJSON *grep_props = cJSON_CreateObject();
    cJSON *grep_pat = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_pat, "type", "string");
    cJSON_AddStringToObject(grep_pat, "description", "The pattern to search for");
    cJSON_AddItemToObject(grep_props, "pattern", grep_pat);
    cJSON_AddItemToObject(grep_params, "properties", grep_props);
    cJSON *grep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(grep_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(grep_params, "required", grep_req);
    Tool grep_search = {
        .name = strdup("grep_search"),
        .description = strdup("Search for a pattern in file contents."),
        .parameters_schema = grep_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_grep_search
    };
    tool_registry_register(reg, grep_search);

    Tool web_fetch = {
        .name = strdup("web_fetch"),
        .description = strdup("Fetch content from a URL."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_web_fetch
    };
    tool_registry_register(reg, web_fetch);

    Tool web_search = {
        .name = strdup("web_search"),
        .description = strdup("Search the web using DuckDuckGo."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_web_search
    };
    tool_registry_register(reg, web_search);

    Tool todo_write = {
        .name = strdup("todo_write"),
        .description = strdup("Create, update, or view a todo list."),
        .parameters_schema = NULL,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_todo_write
    };
    tool_registry_register(reg, todo_write);

    Tool skill = {
        .name = strdup("skill"),
        .description = strdup("Load a skill/skill file for specialized instructions."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_skill
    };
    tool_registry_register(reg, skill);

    Tool agent_tool = {
        .name = strdup("agent"),
        .description = strdup("Spawn a sub-agent to handle a complex task."),
        .parameters_schema = NULL,
        .required_mode = PERM_DANGER_FULL_ACCESS,
        .is_read_only = 0,
        .execute = tool_execute_agent_tool
    };
    tool_registry_register(reg, agent_tool);

    Tool tool_search = {
        .name = strdup("tool_search"),
        .description = strdup("List all available tools and their descriptions."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_tool_search
    };
    tool_registry_register(reg, tool_search);

    Tool notebook_edit = {
        .name = strdup("notebook_edit"),
        .description = strdup("Edit a Jupyter notebook cell."),
        .parameters_schema = NULL,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_notebook_edit
    };
    tool_registry_register(reg, notebook_edit);

    Tool sleep = {
        .name = strdup("sleep"),
        .description = strdup("Wait for a specified duration."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_sleep
    };
    tool_registry_register(reg, sleep);

    Tool send_message = {
        .name = strdup("send_message"),
        .description = strdup("Send a message to the user."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_send_message
    };
    tool_registry_register(reg, send_message);

    Tool config_tool = {
        .name = strdup("config"),
        .description = strdup("View or modify configuration settings."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_config_tool
    };
    tool_registry_register(reg, config_tool);

    Tool structured_out = {
        .name = strdup("structured_output"),
        .description = strdup("Format output as structured JSON."),
        .parameters_schema = NULL,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_structured_out
    };
    tool_registry_register(reg, structured_out);

    Tool repl_tool = {
        .name = strdup("repl"),
        .description = strdup("Execute code in a REPL environment."),
        .parameters_schema = NULL,
        .required_mode = PERM_DANGER_FULL_ACCESS,
        .is_read_only = 0,
        .execute = tool_execute_repl_tool
    };
    tool_registry_register(reg, repl_tool);

    Tool powershell = {
        .name = strdup("powershell"),
        .description = strdup("Execute a PowerShell command."),
        .parameters_schema = NULL,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_powershell
    };
    tool_registry_register(reg, powershell);
}
