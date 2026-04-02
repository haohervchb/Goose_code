#ifndef TOOLS_H
#define TOOLS_H

#include "config.h"
#include "permissions.h"
#include "session.h"
#include "util/cJSON.h"
#include <stdio.h>

typedef struct {
    char *name;
    char *description;
    cJSON *parameters_schema;
    PermissionMode required_mode;
    int is_read_only;
    char *(*execute)(const char *args_json, const GooseConfig *cfg);
} Tool;

typedef struct {
    Tool **tools;
    int count;
    int cap;
} ToolRegistry;

ToolRegistry tool_registry_init(void);
void tool_registry_register(ToolRegistry *reg, Tool tool);
void tool_registry_register_all(ToolRegistry *reg);
void tool_registry_free(ToolRegistry *reg);
cJSON *tool_registry_get_definitions(const ToolRegistry *reg, const GooseConfig *cfg);
Tool *tool_registry_find(const ToolRegistry *reg, const char *name);
char *tool_registry_execute(ToolRegistry *reg, const char *name, const char *args,
                            const GooseConfig *cfg, PermissionCheckResult *perm);
void tool_context_set_session(Session *sess);
Session *tool_context_get_session(void);
char *tool_execute_bash(const char *args, const GooseConfig *cfg);
char *tool_execute_read_file(const char *args, const GooseConfig *cfg);
char *tool_execute_write_file(const char *args, const GooseConfig *cfg);
char *tool_execute_edit_file(const char *args, const GooseConfig *cfg);
char *tool_execute_glob_search(const char *args, const GooseConfig *cfg);
char *tool_execute_grep_search(const char *args, const GooseConfig *cfg);
char *tool_execute_web_fetch(const char *args, const GooseConfig *cfg);
char *tool_execute_web_search(const char *args, const GooseConfig *cfg);
char *tool_execute_todo_write(const char *args, const GooseConfig *cfg);
char *tool_execute_task_create(const char *args, const GooseConfig *cfg);
char *tool_execute_task_get(const char *args, const GooseConfig *cfg);
char *tool_execute_task_list(const char *args, const GooseConfig *cfg);
char *tool_execute_task_update(const char *args, const GooseConfig *cfg);
char *tool_execute_list_mcp_resources(const char *args, const GooseConfig *cfg);
char *tool_execute_read_mcp_resource(const char *args, const GooseConfig *cfg);
char *tool_execute_skill(const char *args, const GooseConfig *cfg);
char *tool_execute_agent_tool(const char *args, const GooseConfig *cfg);
char *tool_execute_tool_search(const char *args, const GooseConfig *cfg);
char *tool_execute_notebook_edit(const char *args, const GooseConfig *cfg);
char *tool_execute_sleep(const char *args, const GooseConfig *cfg);
char *tool_execute_send_message(const char *args, const GooseConfig *cfg);
char *tool_execute_ask_user_question(const char *args, const GooseConfig *cfg);
char *tool_execute_ask_user_question_with_io(const char *args, FILE *input, FILE *output);
char *tool_execute_enter_plan_mode(const char *args, const GooseConfig *cfg);
char *tool_execute_exit_plan_mode(const char *args, const GooseConfig *cfg);
char *tool_execute_config_tool(const char *args, const GooseConfig *cfg);
char *tool_execute_structured_out(const char *args, const GooseConfig *cfg);
char *tool_execute_repl_tool(const char *args, const GooseConfig *cfg);
char *tool_execute_powershell(const char *args, const GooseConfig *cfg);

#endif
