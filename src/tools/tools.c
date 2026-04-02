#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local Session *g_tool_session = NULL;

typedef struct {
    char *key;
    cJSON *defs;
} ToolSchemaCacheEntry;

static ToolSchemaCacheEntry *g_tool_schema_cache = NULL;
static int g_tool_schema_cache_count = 0;
static int g_tool_schema_cache_cap = 0;

static char *tool_schema_cache_key(const GooseConfig *cfg) {
    StrBuf key = strbuf_new();
    strbuf_append_fmt(&key, "%s|%s|%d|", cfg->provider ? cfg->provider : "", cfg->base_url ? cfg->base_url : "", cfg->permission_mode);
    char *allowed = cfg->allowed_tools ? json_to_string(cfg->allowed_tools) : strdup("[]");
    char *denied = cfg->denied_tools ? json_to_string(cfg->denied_tools) : strdup("[]");
    strbuf_append(&key, allowed);
    strbuf_append_char(&key, '|');
    strbuf_append(&key, denied);
    free(allowed);
    free(denied);
    return strbuf_detach(&key);
}

void tool_schema_cache_clear(void) {
    for (int i = 0; i < g_tool_schema_cache_count; i++) {
        free(g_tool_schema_cache[i].key);
        if (g_tool_schema_cache[i].defs) cJSON_Delete(g_tool_schema_cache[i].defs);
    }
    free(g_tool_schema_cache);
    g_tool_schema_cache = NULL;
    g_tool_schema_cache_count = 0;
    g_tool_schema_cache_cap = 0;
}

int tool_schema_cache_size(void) {
    return g_tool_schema_cache_count;
}

void tool_context_set_session(Session *sess) {
    g_tool_session = sess;
}

Session *tool_context_get_session(void) {
    return g_tool_session;
}

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

cJSON *tool_registry_get_definitions(const ToolRegistry *reg, const GooseConfig *cfg) {
    char *cache_key = cfg ? tool_schema_cache_key(cfg) : strdup("default");
    for (int i = 0; i < g_tool_schema_cache_count; i++) {
        if (strcmp(g_tool_schema_cache[i].key, cache_key) == 0) {
            cJSON *cached = cJSON_Duplicate(g_tool_schema_cache[i].defs, 1);
            free(cache_key);
            return cached;
        }
    }

    cJSON *defs = cJSON_CreateArray();
    for (int i = 0; i < reg->count; i++) {
        Tool *t = reg->tools[i];
        if (cfg && !permissions_tool_visible(cfg, t->name, t->required_mode)) {
            continue;
        }
        cJSON *def = json_build_tool_def_openai(t->name, t->description,
                                                   t->parameters_schema ? cJSON_Duplicate(t->parameters_schema, 1) : NULL);
        cJSON_AddItemToArray(defs, def);
    }

    if (g_tool_schema_cache_count + 1 > g_tool_schema_cache_cap) {
        int new_cap = g_tool_schema_cache_cap ? g_tool_schema_cache_cap * 2 : 8;
        g_tool_schema_cache = realloc(g_tool_schema_cache, (size_t)new_cap * sizeof(*g_tool_schema_cache));
        g_tool_schema_cache_cap = new_cap;
    }
    g_tool_schema_cache[g_tool_schema_cache_count].key = cache_key;
    g_tool_schema_cache[g_tool_schema_cache_count].defs = cJSON_Duplicate(defs, 1);
    g_tool_schema_cache_count++;
    return defs;
}

Tool *tool_registry_find(const ToolRegistry *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i]->name, name) == 0) return reg->tools[i];
    }
    return NULL;
}

char *tool_registry_execute_unchecked(ToolRegistry *reg, const char *name, const char *args,
                                      const GooseConfig *cfg) {
    (void)cfg;
    Tool *tool = tool_registry_find(reg, name);
    if (!tool) return strdup("Error: tool not found");
    if (!args) return strdup("Error: missing tool arguments");
    return tool->execute(args, cfg);
}

char *tool_registry_execute(ToolRegistry *reg, const char *name, const char *args,
                            const GooseConfig *cfg, PermissionCheckResult *perm) {
    Tool *tool = tool_registry_find(reg, name);
    if (!tool) return strdup("Error: tool not found");

    *perm = permissions_check(cfg, name, args, tool->required_mode);
    if (*perm == PERM_CHECK_BLOCK) return strdup("Error: tool is blocked by permission policy");
    if (*perm == PERM_CHECK_DENY) return strdup("Error: tool is denied");
    if (*perm == PERM_CHECK_PROMPT) return strdup("Permission required before tool execution.");

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
    cJSON *bash_timeout = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_timeout, "type", "integer");
    cJSON_AddStringToObject(bash_timeout, "description", "Optional timeout in seconds for long-running commands like make or docker build (default 120, max 7200)");
    cJSON_AddItemToObject(bash_props, "timeout", bash_timeout);
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

    cJSON *web_fetch_params = cJSON_CreateObject();
    cJSON_AddStringToObject(web_fetch_params, "type", "object");
    cJSON *web_fetch_props = cJSON_CreateObject();
    cJSON *web_fetch_url = cJSON_CreateObject();
    cJSON_AddStringToObject(web_fetch_url, "type", "string");
    cJSON_AddStringToObject(web_fetch_url, "description", "Fully qualified URL to fetch");
    cJSON_AddItemToObject(web_fetch_props, "url", web_fetch_url);
    cJSON *web_fetch_prompt = cJSON_CreateObject();
    cJSON_AddStringToObject(web_fetch_prompt, "type", "string");
    cJSON_AddStringToObject(web_fetch_prompt, "description", "Optional extraction guidance for the fetched page");
    cJSON_AddItemToObject(web_fetch_props, "prompt", web_fetch_prompt);
    cJSON_AddItemToObject(web_fetch_params, "properties", web_fetch_props);
    cJSON *web_fetch_req = cJSON_CreateArray();
    cJSON_AddItemToArray(web_fetch_req, cJSON_CreateString("url"));
    cJSON_AddItemToObject(web_fetch_params, "required", web_fetch_req);
    Tool web_fetch = {
        .name = strdup("web_fetch"),
        .description = strdup("Fetch content from a URL."),
        .parameters_schema = web_fetch_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_web_fetch
    };
    tool_registry_register(reg, web_fetch);

    cJSON *web_search_params = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_params, "type", "object");
    cJSON *web_search_props = cJSON_CreateObject();
    cJSON *web_search_query = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_query, "type", "string");
    cJSON_AddStringToObject(web_search_query, "description", "Search query");
    cJSON_AddItemToObject(web_search_props, "query", web_search_query);
    cJSON *web_search_allowed = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_allowed, "type", "array");
    cJSON *web_search_domain_item = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_domain_item, "type", "string");
    cJSON_AddItemToObject(web_search_allowed, "items", web_search_domain_item);
    cJSON_AddStringToObject(web_search_allowed, "description", "Optional domain allowlist");
    cJSON_AddItemToObject(web_search_props, "allowed_domains", web_search_allowed);
    cJSON *web_search_blocked = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_blocked, "type", "array");
    cJSON *web_search_blocked_item = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search_blocked_item, "type", "string");
    cJSON_AddItemToObject(web_search_blocked, "items", web_search_blocked_item);
    cJSON_AddStringToObject(web_search_blocked, "description", "Optional domain denylist");
    cJSON_AddItemToObject(web_search_props, "blocked_domains", web_search_blocked);
    cJSON_AddItemToObject(web_search_params, "properties", web_search_props);
    cJSON *web_search_req = cJSON_CreateArray();
    cJSON_AddItemToArray(web_search_req, cJSON_CreateString("query"));
    cJSON_AddItemToObject(web_search_params, "required", web_search_req);
    Tool web_search = {
        .name = strdup("web_search"),
        .description = strdup("Search the web using DuckDuckGo."),
        .parameters_schema = web_search_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_web_search
    };
    tool_registry_register(reg, web_search);

    cJSON *todo_params = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_params, "type", "object");
    cJSON *todo_props = cJSON_CreateObject();
    
    cJSON *todo_item = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_item, "type", "object");
    cJSON *todo_item_props = cJSON_CreateObject();
    cJSON *todo_content = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_content, "type", "string");
    cJSON_AddStringToObject(todo_content, "description", "Content of the todo item");
    cJSON_AddItemToObject(todo_item_props, "content", todo_content);
    cJSON *todo_id = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_id, "type", "string");
    cJSON_AddStringToObject(todo_id, "description", "Optional task id to preserve when rewriting the list");
    cJSON_AddItemToObject(todo_item_props, "id", todo_id);
    cJSON *todo_status = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_status, "type", "string");
    cJSON_AddStringToObject(todo_status, "description", "Status: pending, in_progress, completed, or cancelled");
    cJSON_AddItemToObject(todo_item_props, "status", todo_status);
    cJSON *todo_priority = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_priority, "type", "string");
    cJSON_AddStringToObject(todo_priority, "description", "Priority: high, medium, or low");
    cJSON_AddItemToObject(todo_item_props, "priority", todo_priority);
    cJSON_AddItemToObject(todo_item, "properties", todo_item_props);
    cJSON *todo_item_req = cJSON_CreateArray();
    cJSON_AddItemToArray(todo_item_req, cJSON_CreateString("content"));
    cJSON_AddItemToObject(todo_item, "required", todo_item_req);
    
    cJSON *todo_array = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_array, "type", "array");
    cJSON_AddItemToObject(todo_array, "items", todo_item);
    cJSON_AddItemToObject(todo_props, "todos", todo_array);
    cJSON_AddItemToObject(todo_params, "properties", todo_props);
    cJSON *todo_req = cJSON_CreateArray();
    cJSON_AddItemToArray(todo_req, cJSON_CreateString("todos"));
    cJSON_AddItemToObject(todo_params, "required", todo_req);

    Tool todo_write = {
        .name = strdup("todo_write"),
        .description = strdup("Create, update, or view a todo list."),
        .parameters_schema = todo_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_todo_write
    };
    tool_registry_register(reg, todo_write);

    cJSON *task_create_params = cJSON_CreateObject();
    cJSON_AddStringToObject(task_create_params, "type", "object");
    cJSON *task_create_props = cJSON_CreateObject();
    cJSON *task_create_content = cJSON_CreateObject();
    cJSON_AddStringToObject(task_create_content, "type", "string");
    cJSON_AddStringToObject(task_create_content, "description", "Task content");
    cJSON_AddItemToObject(task_create_props, "content", task_create_content);
    cJSON *task_create_status = cJSON_CreateObject();
    cJSON_AddStringToObject(task_create_status, "type", "string");
    cJSON_AddStringToObject(task_create_status, "description", "Optional initial status");
    cJSON_AddItemToObject(task_create_props, "status", task_create_status);
    cJSON *task_create_priority = cJSON_CreateObject();
    cJSON_AddStringToObject(task_create_priority, "type", "string");
    cJSON_AddStringToObject(task_create_priority, "description", "Optional priority");
    cJSON_AddItemToObject(task_create_props, "priority", task_create_priority);
    cJSON_AddItemToObject(task_create_params, "properties", task_create_props);
    cJSON *task_create_req = cJSON_CreateArray();
    cJSON_AddItemToArray(task_create_req, cJSON_CreateString("content"));
    cJSON_AddItemToObject(task_create_params, "required", task_create_req);
    Tool task_create = {
        .name = strdup("task_create"),
        .description = strdup("Create a single tracked task."),
        .parameters_schema = task_create_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_task_create
    };
    tool_registry_register(reg, task_create);

    cJSON *task_get_params = cJSON_CreateObject();
    cJSON_AddStringToObject(task_get_params, "type", "object");
    cJSON *task_get_props = cJSON_CreateObject();
    cJSON *task_get_id = cJSON_CreateObject();
    cJSON_AddStringToObject(task_get_id, "type", "string");
    cJSON_AddStringToObject(task_get_id, "description", "Task id to retrieve");
    cJSON_AddItemToObject(task_get_props, "task_id", task_get_id);
    cJSON_AddItemToObject(task_get_params, "properties", task_get_props);
    cJSON *task_get_req = cJSON_CreateArray();
    cJSON_AddItemToArray(task_get_req, cJSON_CreateString("task_id"));
    cJSON_AddItemToObject(task_get_params, "required", task_get_req);
    Tool task_get = {
        .name = strdup("task_get"),
        .description = strdup("Fetch a single task by id."),
        .parameters_schema = task_get_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_task_get
    };
    tool_registry_register(reg, task_get);

    cJSON *task_list_params = cJSON_CreateObject();
    cJSON_AddStringToObject(task_list_params, "type", "object");
    cJSON *task_list_props = cJSON_CreateObject();
    cJSON *task_list_status = cJSON_CreateObject();
    cJSON_AddStringToObject(task_list_status, "type", "string");
    cJSON_AddStringToObject(task_list_status, "description", "Optional status filter");
    cJSON_AddItemToObject(task_list_props, "status", task_list_status);
    cJSON *task_list_priority = cJSON_CreateObject();
    cJSON_AddStringToObject(task_list_priority, "type", "string");
    cJSON_AddStringToObject(task_list_priority, "description", "Optional priority filter");
    cJSON_AddItemToObject(task_list_props, "priority", task_list_priority);
    cJSON_AddItemToObject(task_list_params, "properties", task_list_props);
    Tool task_list = {
        .name = strdup("task_list"),
        .description = strdup("List tracked tasks, optionally filtered."),
        .parameters_schema = task_list_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_task_list
    };
    tool_registry_register(reg, task_list);

    cJSON *task_update_params = cJSON_CreateObject();
    cJSON_AddStringToObject(task_update_params, "type", "object");
    cJSON *task_update_props = cJSON_CreateObject();
    cJSON *task_update_id = cJSON_CreateObject();
    cJSON_AddStringToObject(task_update_id, "type", "string");
    cJSON_AddStringToObject(task_update_id, "description", "Task id to update");
    cJSON_AddItemToObject(task_update_props, "task_id", task_update_id);
    cJSON *task_update_content = cJSON_CreateObject();
    cJSON_AddStringToObject(task_update_content, "type", "string");
    cJSON_AddStringToObject(task_update_content, "description", "Optional updated task content");
    cJSON_AddItemToObject(task_update_props, "content", task_update_content);
    cJSON *task_update_status = cJSON_CreateObject();
    cJSON_AddStringToObject(task_update_status, "type", "string");
    cJSON_AddStringToObject(task_update_status, "description", "Optional updated status");
    cJSON_AddItemToObject(task_update_props, "status", task_update_status);
    cJSON *task_update_priority = cJSON_CreateObject();
    cJSON_AddStringToObject(task_update_priority, "type", "string");
    cJSON_AddStringToObject(task_update_priority, "description", "Optional updated priority");
    cJSON_AddItemToObject(task_update_props, "priority", task_update_priority);
    cJSON_AddItemToObject(task_update_params, "properties", task_update_props);
    cJSON *task_update_req = cJSON_CreateArray();
    cJSON_AddItemToArray(task_update_req, cJSON_CreateString("task_id"));
    cJSON_AddItemToObject(task_update_params, "required", task_update_req);
    Tool task_update = {
        .name = strdup("task_update"),
        .description = strdup("Update an existing tracked task."),
        .parameters_schema = task_update_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_task_update
    };
    tool_registry_register(reg, task_update);

    cJSON *mcp_list_params = cJSON_CreateObject();
    cJSON_AddStringToObject(mcp_list_params, "type", "object");
    cJSON *mcp_list_props = cJSON_CreateObject();
    cJSON *mcp_list_server = cJSON_CreateObject();
    cJSON_AddStringToObject(mcp_list_server, "type", "string");
    cJSON_AddStringToObject(mcp_list_server, "description", "Configured MCP server name");
    cJSON_AddItemToObject(mcp_list_props, "server", mcp_list_server);
    cJSON_AddItemToObject(mcp_list_params, "properties", mcp_list_props);
    cJSON *mcp_list_req = cJSON_CreateArray();
    cJSON_AddItemToArray(mcp_list_req, cJSON_CreateString("server"));
    cJSON_AddItemToObject(mcp_list_params, "required", mcp_list_req);
    Tool list_mcp_resources = {
        .name = strdup("list_mcp_resources"),
        .description = strdup("List resources exposed by a configured MCP server."),
        .parameters_schema = mcp_list_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_list_mcp_resources
    };
    tool_registry_register(reg, list_mcp_resources);

    cJSON *mcp_read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(mcp_read_params, "type", "object");
    cJSON *mcp_read_props = cJSON_CreateObject();
    cJSON *mcp_read_server = cJSON_CreateObject();
    cJSON_AddStringToObject(mcp_read_server, "type", "string");
    cJSON_AddStringToObject(mcp_read_server, "description", "Configured MCP server name");
    cJSON_AddItemToObject(mcp_read_props, "server", mcp_read_server);
    cJSON *mcp_read_uri = cJSON_CreateObject();
    cJSON_AddStringToObject(mcp_read_uri, "type", "string");
    cJSON_AddStringToObject(mcp_read_uri, "description", "Resource URI to read from the MCP server");
    cJSON_AddItemToObject(mcp_read_props, "uri", mcp_read_uri);
    cJSON_AddItemToObject(mcp_read_params, "properties", mcp_read_props);
    cJSON *mcp_read_req = cJSON_CreateArray();
    cJSON_AddItemToArray(mcp_read_req, cJSON_CreateString("server"));
    cJSON_AddItemToArray(mcp_read_req, cJSON_CreateString("uri"));
    cJSON_AddItemToObject(mcp_read_params, "required", mcp_read_req);
    Tool read_mcp_resource = {
        .name = strdup("read_mcp_resource"),
        .description = strdup("Read a resource exposed by a configured MCP server."),
        .parameters_schema = mcp_read_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_read_mcp_resource
    };
    tool_registry_register(reg, read_mcp_resource);

    cJSON *lsp_params = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_params, "type", "object");
    cJSON *lsp_props = cJSON_CreateObject();
    cJSON *lsp_action = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_action, "type", "string");
    cJSON_AddStringToObject(lsp_action, "description", "LSP action: hover, definition, or document_symbols");
    cJSON_AddItemToObject(lsp_props, "action", lsp_action);
    cJSON *lsp_file_path = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_file_path, "type", "string");
    cJSON_AddStringToObject(lsp_file_path, "description", "Path to the source file to inspect");
    cJSON_AddItemToObject(lsp_props, "file_path", lsp_file_path);
    cJSON *lsp_line = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_line, "type", "integer");
    cJSON_AddStringToObject(lsp_line, "description", "Zero-based line for hover/definition actions");
    cJSON_AddItemToObject(lsp_props, "line", lsp_line);
    cJSON *lsp_character = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_character, "type", "integer");
    cJSON_AddStringToObject(lsp_character, "description", "Zero-based character for hover/definition actions");
    cJSON_AddItemToObject(lsp_props, "character", lsp_character);
    cJSON *lsp_workspace = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_workspace, "type", "string");
    cJSON_AddStringToObject(lsp_workspace, "description", "Optional workspace root override");
    cJSON_AddItemToObject(lsp_props, "workspace_root", lsp_workspace);
    cJSON *lsp_server_command = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_server_command, "type", "string");
    cJSON_AddStringToObject(lsp_server_command, "description", "Optional explicit language server command");
    cJSON_AddItemToObject(lsp_props, "server_command", lsp_server_command);
    cJSON *lsp_server_args = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_server_args, "type", "array");
    cJSON *lsp_server_arg_item = cJSON_CreateObject();
    cJSON_AddStringToObject(lsp_server_arg_item, "type", "string");
    cJSON_AddItemToObject(lsp_server_args, "items", lsp_server_arg_item);
    cJSON_AddStringToObject(lsp_server_args, "description", "Optional server arguments");
    cJSON_AddItemToObject(lsp_props, "server_args", lsp_server_args);
    cJSON_AddItemToObject(lsp_params, "properties", lsp_props);
    cJSON *lsp_req = cJSON_CreateArray();
    cJSON_AddItemToArray(lsp_req, cJSON_CreateString("action"));
    cJSON_AddItemToArray(lsp_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(lsp_params, "required", lsp_req);
    Tool lsp = {
        .name = strdup("lsp"),
        .description = strdup("Query a language server for hover, definition, or document symbols."),
        .parameters_schema = lsp_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_lsp
    };
    tool_registry_register(reg, lsp);

    cJSON *skill_params = cJSON_CreateObject();
    cJSON_AddStringToObject(skill_params, "type", "object");
    cJSON *skill_props = cJSON_CreateObject();
    cJSON *skill_name = cJSON_CreateObject();
    cJSON_AddStringToObject(skill_name, "type", "string");
    cJSON_AddStringToObject(skill_name, "description", "Skill name to load");
    cJSON_AddItemToObject(skill_props, "name", skill_name);
    cJSON_AddItemToObject(skill_params, "properties", skill_props);
    cJSON *skill_req = cJSON_CreateArray();
    cJSON_AddItemToArray(skill_req, cJSON_CreateString("name"));
    cJSON_AddItemToObject(skill_params, "required", skill_req);
    Tool skill = {
        .name = strdup("skill"),
        .description = strdup("Load a skill/skill file for specialized instructions."),
        .parameters_schema = skill_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_skill
    };
    tool_registry_register(reg, skill);

    cJSON *agent_params = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_params, "type", "object");
    cJSON *agent_props = cJSON_CreateObject();
    cJSON *agent_prompt = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_prompt, "type", "string");
    cJSON_AddStringToObject(agent_prompt, "description", "Prompt to hand to the sub-agent");
    cJSON_AddItemToObject(agent_props, "prompt", agent_prompt);
    cJSON *agent_name = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_name, "type", "string");
    cJSON_AddStringToObject(agent_name, "description", "Optional sub-agent name");
    cJSON_AddItemToObject(agent_props, "name", agent_name);
    cJSON *agent_desc = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_desc, "type", "string");
    cJSON_AddStringToObject(agent_desc, "description", "Short description of the delegated task");
    cJSON_AddItemToObject(agent_props, "description", agent_desc);
    cJSON *agent_type = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_type, "type", "string");
    cJSON_AddStringToObject(agent_type, "description", "Sub-agent type such as general or explore");
    cJSON_AddItemToObject(agent_props, "subagent_type", agent_type);
    cJSON *agent_model = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_model, "type", "string");
    cJSON_AddStringToObject(agent_model, "description", "Optional model override");
    cJSON_AddItemToObject(agent_props, "model", agent_model);
    cJSON *agent_working_dir = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_working_dir, "type", "string");
    cJSON_AddStringToObject(agent_working_dir, "description", "Optional working directory override for the subagent");
    cJSON_AddItemToObject(agent_props, "working_dir", agent_working_dir);
    cJSON *agent_use_worktree = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_use_worktree, "type", "boolean");
    cJSON_AddStringToObject(agent_use_worktree, "description", "Create an isolated git worktree for the subagent when possible");
    cJSON_AddItemToObject(agent_props, "use_worktree", agent_use_worktree);
    cJSON *agent_task_id = cJSON_CreateObject();
    cJSON_AddStringToObject(agent_task_id, "type", "string");
    cJSON_AddStringToObject(agent_task_id, "description", "Optional existing subagent task_id to resume");
    cJSON_AddItemToObject(agent_props, "task_id", agent_task_id);
    cJSON_AddItemToObject(agent_params, "properties", agent_props);
    Tool agent_tool = {
        .name = strdup("agent"),
        .description = strdup("Spawn a sub-agent to handle a complex task."),
        .parameters_schema = agent_params,
        .required_mode = PERM_DANGER_FULL_ACCESS,
        .is_read_only = 0,
        .execute = tool_execute_agent_tool
    };
    tool_registry_register(reg, agent_tool);

    cJSON *tool_search_params = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_search_params, "type", "object");
    cJSON *tool_search_props = cJSON_CreateObject();
    cJSON *tool_search_query = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_search_query, "type", "string");
    cJSON_AddStringToObject(tool_search_query, "description", "Optional search query for matching tools");
    cJSON_AddItemToObject(tool_search_props, "query", tool_search_query);
    cJSON *tool_search_max = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_search_max, "type", "integer");
    cJSON_AddStringToObject(tool_search_max, "description", "Maximum number of results to return");
    cJSON_AddItemToObject(tool_search_props, "max_results", tool_search_max);
    cJSON_AddItemToObject(tool_search_params, "properties", tool_search_props);
    Tool tool_search = {
        .name = strdup("tool_search"),
        .description = strdup("List all available tools and their descriptions."),
        .parameters_schema = tool_search_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_tool_search
    };
    tool_registry_register(reg, tool_search);

    cJSON *notebook_params = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_params, "type", "object");
    cJSON *notebook_props = cJSON_CreateObject();
    cJSON *notebook_path = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_path, "type", "string");
    cJSON_AddStringToObject(notebook_path, "description", "Path to the notebook file");
    cJSON_AddItemToObject(notebook_props, "notebook_path", notebook_path);
    cJSON *notebook_cell = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_cell, "type", "string");
    cJSON_AddStringToObject(notebook_cell, "description", "Optional cell id to edit");
    cJSON_AddItemToObject(notebook_props, "cell_id", notebook_cell);
    cJSON *notebook_source = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_source, "type", "string");
    cJSON_AddStringToObject(notebook_source, "description", "New cell source content");
    cJSON_AddItemToObject(notebook_props, "new_source", notebook_source);
    cJSON *notebook_cell_type = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_cell_type, "type", "string");
    cJSON_AddStringToObject(notebook_cell_type, "description", "Optional new cell type");
    cJSON_AddItemToObject(notebook_props, "cell_type", notebook_cell_type);
    cJSON *notebook_mode = cJSON_CreateObject();
    cJSON_AddStringToObject(notebook_mode, "type", "string");
    cJSON_AddStringToObject(notebook_mode, "description", "Edit mode, typically replace");
    cJSON_AddItemToObject(notebook_props, "edit_mode", notebook_mode);
    cJSON_AddItemToObject(notebook_params, "properties", notebook_props);
    cJSON *notebook_req = cJSON_CreateArray();
    cJSON_AddItemToArray(notebook_req, cJSON_CreateString("notebook_path"));
    cJSON_AddItemToArray(notebook_req, cJSON_CreateString("new_source"));
    cJSON_AddItemToObject(notebook_params, "required", notebook_req);
    Tool notebook_edit = {
        .name = strdup("notebook_edit"),
        .description = strdup("Edit a Jupyter notebook cell."),
        .parameters_schema = notebook_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_notebook_edit
    };
    tool_registry_register(reg, notebook_edit);

    cJSON *sleep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_params, "type", "object");
    cJSON *sleep_props = cJSON_CreateObject();
    cJSON *sleep_duration = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_duration, "type", "integer");
    cJSON_AddStringToObject(sleep_duration, "description", "Duration to sleep in milliseconds");
    cJSON_AddItemToObject(sleep_props, "duration_ms", sleep_duration);
    cJSON_AddItemToObject(sleep_params, "properties", sleep_props);
    Tool sleep = {
        .name = strdup("sleep"),
        .description = strdup("Wait for a specified duration."),
        .parameters_schema = sleep_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_sleep
    };
    tool_registry_register(reg, sleep);

    cJSON *send_params = cJSON_CreateObject();
    cJSON_AddStringToObject(send_params, "type", "object");
    cJSON *send_props = cJSON_CreateObject();
    cJSON *send_message_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(send_message_prop, "type", "string");
    cJSON_AddStringToObject(send_message_prop, "description", "Message to show the user");
    cJSON_AddItemToObject(send_props, "message", send_message_prop);
    cJSON *send_status_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(send_status_prop, "type", "string");
    cJSON_AddStringToObject(send_status_prop, "description", "Optional status prefix");
    cJSON_AddItemToObject(send_props, "status", send_status_prop);
    cJSON *send_attachments_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(send_attachments_prop, "type", "array");
    cJSON *send_attachment_item = cJSON_CreateObject();
    cJSON_AddStringToObject(send_attachment_item, "type", "object");
    cJSON *send_attachment_props = cJSON_CreateObject();
    cJSON *send_attachment_path = cJSON_CreateObject();
    cJSON_AddStringToObject(send_attachment_path, "type", "string");
    cJSON_AddStringToObject(send_attachment_path, "description", "Attachment path");
    cJSON_AddItemToObject(send_attachment_props, "path", send_attachment_path);
    cJSON_AddItemToObject(send_attachment_item, "properties", send_attachment_props);
    cJSON_AddItemToObject(send_attachments_prop, "items", send_attachment_item);
    cJSON_AddItemToObject(send_props, "attachments", send_attachments_prop);
    cJSON_AddItemToObject(send_params, "properties", send_props);
    cJSON *send_req = cJSON_CreateArray();
    cJSON_AddItemToArray(send_req, cJSON_CreateString("message"));
    cJSON_AddItemToObject(send_params, "required", send_req);
    Tool send_message = {
        .name = strdup("send_message"),
        .description = strdup("Send a message to the user."),
        .parameters_schema = send_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_send_message
    };
    tool_registry_register(reg, send_message);

    cJSON *question_params = cJSON_CreateObject();
    cJSON_AddStringToObject(question_params, "type", "object");
    cJSON *question_props = cJSON_CreateObject();
    cJSON *questions_array = cJSON_CreateObject();
    cJSON_AddStringToObject(questions_array, "type", "array");
    cJSON *question_item = cJSON_CreateObject();
    cJSON_AddStringToObject(question_item, "type", "object");
    cJSON *question_item_props = cJSON_CreateObject();

    cJSON *question_text = cJSON_CreateObject();
    cJSON_AddStringToObject(question_text, "type", "string");
    cJSON_AddStringToObject(question_text, "description", "Question to ask the user");
    cJSON_AddItemToObject(question_item_props, "question", question_text);

    cJSON *question_header = cJSON_CreateObject();
    cJSON_AddStringToObject(question_header, "type", "string");
    cJSON_AddStringToObject(question_header, "description", "Short header for the question");
    cJSON_AddItemToObject(question_item_props, "header", question_header);

    cJSON *question_multiple = cJSON_CreateObject();
    cJSON_AddStringToObject(question_multiple, "type", "boolean");
    cJSON_AddStringToObject(question_multiple, "description", "Allow multiple selections");
    cJSON_AddItemToObject(question_item_props, "multiple", question_multiple);

    cJSON *question_custom = cJSON_CreateObject();
    cJSON_AddStringToObject(question_custom, "type", "boolean");
    cJSON_AddStringToObject(question_custom, "description", "Allow custom typed answers");
    cJSON_AddItemToObject(question_item_props, "custom", question_custom);

    cJSON *options_array = cJSON_CreateObject();
    cJSON_AddStringToObject(options_array, "type", "array");
    cJSON *option_item = cJSON_CreateObject();
    cJSON_AddStringToObject(option_item, "type", "object");
    cJSON *option_props = cJSON_CreateObject();

    cJSON *option_label = cJSON_CreateObject();
    cJSON_AddStringToObject(option_label, "type", "string");
    cJSON_AddStringToObject(option_label, "description", "Display label for the option");
    cJSON_AddItemToObject(option_props, "label", option_label);

    cJSON *option_description = cJSON_CreateObject();
    cJSON_AddStringToObject(option_description, "type", "string");
    cJSON_AddStringToObject(option_description, "description", "Short explanation of the option");
    cJSON_AddItemToObject(option_props, "description", option_description);

    cJSON_AddItemToObject(option_item, "properties", option_props);
    cJSON *option_required = cJSON_CreateArray();
    cJSON_AddItemToArray(option_required, cJSON_CreateString("label"));
    cJSON_AddItemToObject(option_item, "required", option_required);
    cJSON_AddItemToObject(options_array, "items", option_item);
    cJSON_AddItemToObject(question_item_props, "options", options_array);

    cJSON_AddItemToObject(question_item, "properties", question_item_props);
    cJSON *question_required = cJSON_CreateArray();
    cJSON_AddItemToArray(question_required, cJSON_CreateString("question"));
    cJSON_AddItemToArray(question_required, cJSON_CreateString("header"));
    cJSON_AddItemToArray(question_required, cJSON_CreateString("options"));
    cJSON_AddItemToArray(question_required, cJSON_CreateString("multiple"));
    cJSON_AddItemToObject(question_item, "required", question_required);
    cJSON_AddItemToObject(questions_array, "items", question_item);
    cJSON_AddItemToObject(question_props, "questions", questions_array);
    cJSON_AddItemToObject(question_params, "properties", question_props);
    cJSON *question_req = cJSON_CreateArray();
    cJSON_AddItemToArray(question_req, cJSON_CreateString("questions"));
    cJSON_AddItemToObject(question_params, "required", question_req);

    Tool ask_user_question = {
        .name = strdup("ask_user_question"),
        .description = strdup("Ask the user a structured question and wait for an answer."),
        .parameters_schema = question_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_ask_user_question
    };
    tool_registry_register(reg, ask_user_question);

    cJSON *enter_plan_params = cJSON_CreateObject();
    cJSON_AddStringToObject(enter_plan_params, "type", "object");
    cJSON *enter_plan_props = cJSON_CreateObject();
    cJSON *enter_plan_plan = cJSON_CreateObject();
    cJSON_AddStringToObject(enter_plan_plan, "type", "string");
    cJSON_AddStringToObject(enter_plan_plan, "description", "Optional plan text to store when entering plan mode");
    cJSON_AddItemToObject(enter_plan_props, "plan", enter_plan_plan);
    cJSON *enter_plan_desc = cJSON_CreateObject();
    cJSON_AddStringToObject(enter_plan_desc, "type", "string");
    cJSON_AddStringToObject(enter_plan_desc, "description", "Optional alias for the plan text");
    cJSON_AddItemToObject(enter_plan_props, "description", enter_plan_desc);
    cJSON_AddItemToObject(enter_plan_params, "properties", enter_plan_props);
    Tool enter_plan_mode = {
        .name = strdup("enter_plan_mode"),
        .description = strdup("Enable session plan mode and optionally store a plan."),
        .parameters_schema = enter_plan_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_enter_plan_mode
    };
    tool_registry_register(reg, enter_plan_mode);

    cJSON *exit_plan_params = cJSON_CreateObject();
    cJSON_AddStringToObject(exit_plan_params, "type", "object");
    cJSON_AddItemToObject(exit_plan_params, "properties", cJSON_CreateObject());
    Tool exit_plan_mode = {
        .name = strdup("exit_plan_mode"),
        .description = strdup("Disable session plan mode."),
        .parameters_schema = exit_plan_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_exit_plan_mode
    };
    tool_registry_register(reg, exit_plan_mode);

    cJSON *config_params = cJSON_CreateObject();
    cJSON_AddStringToObject(config_params, "type", "object");
    cJSON *config_props = cJSON_CreateObject();
    cJSON *config_setting = cJSON_CreateObject();
    cJSON_AddStringToObject(config_setting, "type", "string");
    cJSON_AddStringToObject(config_setting, "description", "Setting name to read or update");
    cJSON_AddItemToObject(config_props, "setting", config_setting);
    cJSON *config_value = cJSON_CreateObject();
    cJSON_AddStringToObject(config_value, "type", "string");
    cJSON_AddStringToObject(config_value, "description", "Optional new value for the setting");
    cJSON_AddItemToObject(config_props, "value", config_value);
    cJSON_AddItemToObject(config_params, "properties", config_props);
    Tool config_tool = {
        .name = strdup("config"),
        .description = strdup("View or modify configuration settings."),
        .parameters_schema = config_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_config_tool
    };
    tool_registry_register(reg, config_tool);

    cJSON *structured_params = cJSON_CreateObject();
    cJSON_AddStringToObject(structured_params, "type", "object");
    cJSON *structured_props = cJSON_CreateObject();
    cJSON *structured_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(structured_schema, "type", "string");
    cJSON_AddStringToObject(structured_schema, "description", "Schema description for the desired output");
    cJSON_AddItemToObject(structured_props, "schema", structured_schema);
    cJSON *structured_data = cJSON_CreateObject();
    cJSON_AddStringToObject(structured_data, "type", "string");
    cJSON_AddStringToObject(structured_data, "description", "JSON or text payload to format");
    cJSON_AddItemToObject(structured_props, "data", structured_data);
    cJSON_AddItemToObject(structured_params, "properties", structured_props);
    Tool structured_out = {
        .name = strdup("structured_output"),
        .description = strdup("Format output as structured JSON."),
        .parameters_schema = structured_params,
        .required_mode = PERM_READ_ONLY,
        .is_read_only = 1,
        .execute = tool_execute_structured_out
    };
    tool_registry_register(reg, structured_out);

    cJSON *repl_params = cJSON_CreateObject();
    cJSON_AddStringToObject(repl_params, "type", "object");
    cJSON *repl_props = cJSON_CreateObject();
    cJSON *repl_language = cJSON_CreateObject();
    cJSON_AddStringToObject(repl_language, "type", "string");
    cJSON_AddStringToObject(repl_language, "description", "Interpreter language such as python or node");
    cJSON_AddItemToObject(repl_props, "language", repl_language);
    cJSON *repl_code = cJSON_CreateObject();
    cJSON_AddStringToObject(repl_code, "type", "string");
    cJSON_AddStringToObject(repl_code, "description", "Code to execute in the REPL");
    cJSON_AddItemToObject(repl_props, "code", repl_code);
    cJSON_AddItemToObject(repl_params, "properties", repl_props);
    cJSON *repl_req = cJSON_CreateArray();
    cJSON_AddItemToArray(repl_req, cJSON_CreateString("code"));
    cJSON_AddItemToObject(repl_params, "required", repl_req);
    Tool repl_tool = {
        .name = strdup("repl"),
        .description = strdup("Execute code in a REPL environment."),
        .parameters_schema = repl_params,
        .required_mode = PERM_DANGER_FULL_ACCESS,
        .is_read_only = 0,
        .execute = tool_execute_repl_tool
    };
    tool_registry_register(reg, repl_tool);

    cJSON *powershell_params = cJSON_CreateObject();
    cJSON_AddStringToObject(powershell_params, "type", "object");
    cJSON *powershell_props = cJSON_CreateObject();
    cJSON *powershell_command = cJSON_CreateObject();
    cJSON_AddStringToObject(powershell_command, "type", "string");
    cJSON_AddStringToObject(powershell_command, "description", "PowerShell command to execute");
    cJSON_AddItemToObject(powershell_props, "command", powershell_command);
    cJSON *powershell_timeout = cJSON_CreateObject();
    cJSON_AddStringToObject(powershell_timeout, "type", "integer");
    cJSON_AddStringToObject(powershell_timeout, "description", "Approximate timeout in seconds");
    cJSON_AddItemToObject(powershell_props, "timeout", powershell_timeout);
    cJSON_AddItemToObject(powershell_params, "properties", powershell_props);
    cJSON *powershell_req = cJSON_CreateArray();
    cJSON_AddItemToArray(powershell_req, cJSON_CreateString("command"));
    cJSON_AddItemToObject(powershell_params, "required", powershell_req);
    Tool powershell = {
        .name = strdup("powershell"),
        .description = strdup("Execute a PowerShell command."),
        .parameters_schema = powershell_params,
        .required_mode = PERM_WORKSPACE_WRITE,
        .is_read_only = 0,
        .execute = tool_execute_powershell
    };
    tool_registry_register(reg, powershell);
}
