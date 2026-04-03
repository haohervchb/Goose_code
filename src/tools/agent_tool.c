#include "tools/tools.h"
#include "tools/subagent_store.h"
#include "api.h"
#include "prompt.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int subagent_type_valid(const char *type) {
    return strcmp(type, "general") == 0 || strcmp(type, "explore") == 0 || strcmp(type, "plan") == 0;
}

static int name_in_list(const char *name, const char *const *list, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(name, list[i]) == 0) return 1;
    }
    return 0;
}

static void replace_owned_string(char **slot, const char *value) {
    free(*slot);
    *slot = value ? strdup(value) : NULL;
}

static void set_record_status(SubagentRecord *record, const char *status) {
    replace_owned_string(&record->status, status);
}

static int parse_bool_arg(const cJSON *json, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem((cJSON *)json, key);
    if (!item) return default_value;
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return default_value;
}

static char *quote_arg(const char *value) {
    size_t len = strlen(value);
    size_t extra = 2;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') extra += 3;
    }

    char *out = malloc(len + extra + 1);
    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = value[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

static char *run_capture_cmd(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    StrBuf out = strbuf_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        strbuf_append(&out, buf);
    }

    int rc = pclose(fp);
    if (rc != 0) {
        strbuf_free(&out);
        return NULL;
    }

    while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\r')) {
        out.data[--out.len] = '\0';
    }

    return strbuf_detach(&out);
}

static char *git_repo_root(const char *working_dir) {
    char *quoted = quote_arg(working_dir);
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "git -C %s rev-parse --show-toplevel 2>/dev/null", quoted);
    free(quoted);
    return run_capture_cmd(cmd);
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static char *make_worktree_path(const GooseConfig *cfg, const char *task_id) {
    size_t len = strlen(cfg->worktree_dir) + strlen(task_id) + 16;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", cfg->worktree_dir, task_id);
    return path;
}

static char *ensure_subagent_working_dir(SubagentRecord *record, const GooseConfig *cfg,
                                         const char *requested_working_dir, int use_worktree) {
    if (record->working_dir && record->working_dir[0]) return NULL;

    const char *base_dir = (requested_working_dir && requested_working_dir[0]) ? requested_working_dir : cfg->working_dir;
    if (!use_worktree) {
        replace_owned_string(&record->working_dir, base_dir);
        replace_owned_string(&record->workspace_mode, "direct");
        return NULL;
    }

    char *repo_root = git_repo_root(base_dir);
    if (!repo_root) return strdup("Error: use_worktree requires a git repository working directory");

    char *worktree_path = make_worktree_path(cfg, record->task_id);
    if (!path_exists(worktree_path)) {
        char *quoted_repo = quote_arg(repo_root);
        char *quoted_path = quote_arg(worktree_path);
        char cmd[8192];
        snprintf(cmd, sizeof(cmd), "git -C %s worktree add --detach %s HEAD >/dev/null 2>&1", quoted_repo, quoted_path);
        int rc = system(cmd);
        free(quoted_repo);
        free(quoted_path);
        if (rc != 0 || !path_exists(worktree_path)) {
            free(repo_root);
            free(worktree_path);
            return strdup("Error: failed to create git worktree for subagent");
        }
    }

    replace_owned_string(&record->working_dir, worktree_path);
    replace_owned_string(&record->workspace_mode, "git_worktree");
    free(repo_root);
    free(worktree_path);
    return NULL;
}

static int subagent_tool_allowed(const Tool *tool, const GooseConfig *cfg, const char *subagent_type) {
    static const char *const general_tools[] = {
        "bash", "read_file", "write_file", "edit_file", "glob_search", "grep_search",
        "web_fetch", "web_search", "todo_write", "task_create", "task_get", "task_list",
        "task_update", "lsp", "skill", "tool_search", "sleep"
    };
    static const char *const explore_tools[] = {
        "read_file", "glob_search", "grep_search", "web_fetch", "web_search", "lsp",
        "task_get", "task_list", "skill", "tool_search", "sleep"
    };
    static const char *const plan_tools[] = {
        "read_file", "glob_search", "grep_search", "web_fetch", "web_search", "lsp",
        "task_get", "task_list", "tool_search", "sleep"
    };

    if (!permissions_tool_visible(cfg, tool->name, tool->required_mode)) return 0;

    if (strcmp(subagent_type, "general") == 0) {
        return name_in_list(tool->name, general_tools, (int)(sizeof(general_tools) / sizeof(general_tools[0])));
    }
    if (strcmp(subagent_type, "explore") == 0) {
        return name_in_list(tool->name, explore_tools, (int)(sizeof(explore_tools) / sizeof(explore_tools[0])));
    }
    return name_in_list(tool->name, plan_tools, (int)(sizeof(plan_tools) / sizeof(plan_tools[0])));
}

static cJSON *subagent_tool_definitions(const ToolRegistry *reg, const GooseConfig *cfg,
                                        const char *subagent_type) {
    cJSON *defs = cJSON_CreateArray();
    for (int i = 0; i < reg->count; i++) {
        Tool *tool = reg->tools[i];
        if (!subagent_tool_allowed(tool, cfg, subagent_type)) continue;
        cJSON *def = json_build_tool_def_openai(tool->name, tool->description,
                                                tool->parameters_schema ? cJSON_Duplicate(tool->parameters_schema, 1) : NULL);
        cJSON_AddItemToArray(defs, def);
    }
    return defs;
}

char *subagent_system_prompt(const GooseConfig *cfg, const SubagentRecord *record) {
    char *base = prompt_build_default_system(cfg, NULL, record->working_dir ? record->working_dir : cfg->working_dir);
    StrBuf instructions = strbuf_new();

    strbuf_append(&instructions, base);
    free(base);
    strbuf_append(&instructions, "\n## Subagent Mode\n");
    strbuf_append(&instructions, "- You are a delegated subagent working for a parent goosecode session\n");
    strbuf_append(&instructions, "- Complete the delegated task autonomously and return a concise final answer for the parent\n");
    strbuf_append(&instructions, "- Do not address the human directly or ask follow-up questions unless blocked\n");
    strbuf_append(&instructions, "- Never spawn another subagent\n");

    if (record->fork_mode) {
        strbuf_append(&instructions, "- Fork mode: You inherit the parent session's conversation context and are continuing its work\n");
        strbuf_append(&instructions, "- Use the parent's prior messages as background context, but focus on the delegated task\n");
    } else {
        strbuf_append(&instructions, "- The parent chose to delegate because this task is better handled in a fresh subagent context\n");
    }

    if (record->description) {
        strbuf_append_fmt(&instructions, "- Delegated task: %s\n", record->description);
    }

    strbuf_append(&instructions, "\n## Writing And Reasoning Rules\n");
    strbuf_append(&instructions, "- Treat the delegated prompt as your full assignment; do not assume hidden context beyond it and the visible tools\n");
    strbuf_append(&instructions, "- Do not delegate understanding back to the parent. Read, inspect, and reason enough to make concrete judgment calls yourself\n");
    strbuf_append(&instructions, "- Prefer explicit file paths, commands, and findings over vague summaries\n");
    strbuf_append(&instructions, "- If you are blocked, explain exactly what is missing or what failed\n");
    strbuf_append(&instructions, "- If the delegated prompt asks for a short answer, keep the final answer short\n");
    strbuf_append(&instructions, "- Avoid unnecessary tool calls once you have enough information to answer\n");

    if (record->subagent_type && strcmp(record->subagent_type, "explore") == 0) {
        strbuf_append(&instructions, "- This is an explore subagent: prefer searching, reading, and analysis over editing\n");
        strbuf_append(&instructions, "- Your job is to investigate and report findings, not to make speculative changes\n");
    } else if (record->subagent_type && strcmp(record->subagent_type, "plan") == 0) {
        strbuf_append(&instructions, "- This is a plan subagent: focus on producing a clear implementation or investigation plan\n");
        strbuf_append(&instructions, "- Prefer read-only inspection and return actionable next steps\n");
        strbuf_append(&instructions, "- Your output should make it easy for the parent to continue the work immediately\n");
    } else {
        strbuf_append(&instructions, "- This is a general subagent: solve the task end-to-end within the allowed tools\n");
        strbuf_append(&instructions, "- Be decisive: gather the minimum context required, do the work, and report the concrete result\n");
    }

    if (!record->fork_mode) {
        strbuf_append(&instructions, "\n## Fresh Context Reminder\n");
        strbuf_append(&instructions, "- You are not continuing the entire parent transcript; you are acting from the delegated prompt plus the files and tools you inspect\n");
        strbuf_append(&instructions, "- If the delegated prompt lacks critical context, say what is missing instead of inventing it\n");
    }

    return strbuf_detach(&instructions);
}

static char *subagent_execute_tool(ToolRegistry *reg, const GooseConfig *cfg,
                                   const char *subagent_type, const char *name, const char *args) {
    Tool *tool = tool_registry_find(reg, name);
    if (!tool) return strdup("Error: tool not found");
    if (!subagent_tool_allowed(tool, cfg, subagent_type)) {
        return strdup("Error: tool not allowed for this subagent type");
    }

    PermissionCheckResult perm = permissions_check(cfg, name, args, tool->required_mode);
    if (perm == PERM_CHECK_BLOCK) return strdup("Error: tool is blocked by permission policy");
    if (perm == PERM_CHECK_DENY) return strdup("Error: tool is denied");
    if (perm == PERM_CHECK_PROMPT) {
        return strdup("Error: subagent cannot request interactive permission approval; rerun with a less restrictive permission mode");
    }

    tool_context_set_session(NULL);
    char *result = tool->execute(args, cfg);
    tool_context_set_session(NULL);
    return result ? result : strdup("Error: tool returned no result");
}

static void subagent_append_message(cJSON *messages, cJSON *msg) {
    cJSON_AddItemToArray(messages, msg);
}

static char *subagent_sanitize_result(const char *text) {
    const char *p = text ? text : "";
    const char *last_end = NULL;
    const char *scan = p;
    while ((scan = strstr(scan, "</think>")) != NULL) {
        last_end = scan;
        scan += 8;
    }
    if (last_end) p = last_end + 8;

    while (*p && isspace((unsigned char)*p)) p++;
    return strdup(p);
}

static int subagent_execute_calls(SubagentRecord *record, ToolRegistry *reg, const GooseConfig *cfg,
                                  cJSON *tool_calls) {
    cJSON *assistant_msg = json_build_tool_message("assistant", cJSON_Duplicate(tool_calls, 1));
    subagent_append_message(record->messages, assistant_msg);

    cJSON *call;
    cJSON_ArrayForEach(call, tool_calls) {
        const char *tool_call_id = json_get_string(call, "id");
        cJSON *fn = json_get_object(call, "function");
        const char *name = fn ? json_get_string(fn, "name") : NULL;
        const char *args = fn ? json_get_string(fn, "arguments") : NULL;
        if (!tool_call_id || !name) {
            subagent_append_message(record->messages,
                                    json_build_tool_result(tool_call_id ? tool_call_id : "unknown",
                                                           "Error: malformed tool call"));
            continue;
        }

        char *result = subagent_execute_tool(reg, cfg, record->subagent_type ? record->subagent_type : "general",
                                             name, args ? args : "{}");
        subagent_append_message(record->messages, json_build_tool_result(tool_call_id, result));
        free(result);
    }

    return 0;
}

static char *subagent_result_json(const SubagentRecord *record) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "task_id", record->task_id);
    cJSON_AddStringToObject(result, "status", record->status ? record->status : "pending");
    if (record->description) cJSON_AddStringToObject(result, "description", record->description);
    if (record->subagent_type) cJSON_AddStringToObject(result, "subagent_type", record->subagent_type);
    if (record->model) cJSON_AddStringToObject(result, "model", record->model);
    if (record->working_dir) cJSON_AddStringToObject(result, "working_dir", record->working_dir);
    if (record->workspace_mode) cJSON_AddStringToObject(result, "workspace_mode", record->workspace_mode);
    if (record->result) cJSON_AddStringToObject(result, "result", record->result);
    if (record->error) cJSON_AddStringToObject(result, "error", record->error);
    char *out = json_to_string(result);
    cJSON_Delete(result);
    return out ? out : strdup("Error: failed to encode subagent result");
}

static char *subagent_run(SubagentRecord *record, const GooseConfig *cfg) {
    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);
    cJSON *tools_def = subagent_tool_definitions(&reg, cfg, record->subagent_type ? record->subagent_type : "general");
    char *system_prompt = subagent_system_prompt(cfg, record);
    cJSON *system_msg = json_build_message("system", system_prompt);
    free(system_prompt);

    ApiConfig api_cfg = {0};
    api_cfg.base_url = cfg->base_url;
    api_cfg.api_key = cfg->api_key;
    api_cfg.model = record->model ? record->model : cfg->model;
    api_cfg.max_tokens = cfg->max_tokens;
    api_cfg.temperature = cfg->temperature;
    api_cfg.max_retries = 2;

    set_record_status(record, "running");
    replace_owned_string(&record->error, NULL);
    char *save_err = subagent_record_save(cfg, record);
    free(save_err);

    int max_turns = cfg->max_turns > 0 ? cfg->max_turns : 8;
    if (max_turns > 12) max_turns = 12;

    for (int turn = 0; turn < max_turns; turn++) {
        cJSON *messages = prompt_build_messages_with_tools(system_msg, record->messages, NULL);
        ApiResponse resp = api_send_message(&api_cfg, messages, tools_def);
        cJSON_Delete(messages);

        if (resp.status != API_OK) {
            set_record_status(record, "error");
            replace_owned_string(&record->error, resp.error ? resp.error : api_status_str(resp.status));
            api_response_free(&resp);
            subagent_record_save(cfg, record);
            cJSON_Delete(system_msg);
            cJSON_Delete(tools_def);
            tool_registry_free(&reg);
            return subagent_result_json(record);
        }

        if (resp.tool_calls && cJSON_GetArraySize(resp.tool_calls) > 0) {
            subagent_execute_calls(record, &reg, cfg, resp.tool_calls);
            api_response_free(&resp);
            save_err = subagent_record_save(cfg, record);
            free(save_err);
            continue;
        }

        char *clean = subagent_sanitize_result(resp.text_content.data ? resp.text_content.data : "");
        subagent_append_message(record->messages, json_build_message("assistant", clean));
        replace_owned_string(&record->result, clean);
        set_record_status(record, "completed");
        free(clean);
        api_response_free(&resp);
        subagent_record_save(cfg, record);
        cJSON_Delete(system_msg);
        cJSON_Delete(tools_def);
        tool_registry_free(&reg);
        return subagent_result_json(record);
    }

    set_record_status(record, "error");
    replace_owned_string(&record->error, "Subagent hit max turns before producing a final answer");
    subagent_record_save(cfg, record);
    cJSON_Delete(system_msg);
    cJSON_Delete(tools_def);
    tool_registry_free(&reg);
    return subagent_result_json(record);
}

static char *new_subagent_id(void) {
    char id[64];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(id, sizeof(id), "subagent_%ld_%ld", (long)ts.tv_sec, (long)ts.tv_nsec);
    return strdup(id);
}

char *tool_execute_agent_tool(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *prompt = json_get_string(json, "prompt");
    const char *description = json_get_string(json, "description");
    const char *subagent_type = json_get_string(json, "subagent_type");
    const char *model = json_get_string(json, "model");
    const char *task_id = json_get_string(json, "task_id");
    const char *requested_working_dir = json_get_string(json, "working_dir");
    int use_worktree = parse_bool_arg(json, "use_worktree", 0);
    int fork_mode = parse_bool_arg(json, "fork", 0);

    const char *effective_type = subagent_type ? subagent_type : "general";
    if (!subagent_type_valid(effective_type)) {
        cJSON_Delete(json);
        return strdup("Error: subagent_type must be one of general, explore, or plan");
    }

    SubagentRecord *record = NULL;
    if (task_id && task_id[0]) {
        record = subagent_record_load(cfg, task_id);
        if (!record) {
            cJSON_Delete(json);
            return strdup("Error: subagent task_id not found");
        }
    } else {
        if (!prompt || !prompt[0]) {
            cJSON_Delete(json);
            return strdup("Error: 'prompt' argument required for a new subagent");
        }
        if (!description || !description[0]) {
            cJSON_Delete(json);
            return strdup("Error: 'description' argument required for a new subagent");
        }

        char *generated_id = new_subagent_id();
        record = subagent_record_new(generated_id);
        free(generated_id);
        replace_owned_string(&record->description, description);
        replace_owned_string(&record->subagent_type, effective_type);
        replace_owned_string(&record->model, model ? model : cfg->model);
        record->fork_mode = fork_mode;
    }

    if (description && description[0] && !record->description) {
        replace_owned_string(&record->description, description);
    }
    if (subagent_type && subagent_type[0]) {
        replace_owned_string(&record->subagent_type, subagent_type);
    } else if (!record->subagent_type) {
        replace_owned_string(&record->subagent_type, effective_type);
    }
    if (model && model[0]) {
        replace_owned_string(&record->model, model);
    } else if (!record->model) {
        replace_owned_string(&record->model, cfg->model);
    }
    char *workdir_err = ensure_subagent_working_dir(record, cfg, requested_working_dir, use_worktree);
    if (workdir_err) {
        subagent_record_free(record);
        cJSON_Delete(json);
        return workdir_err;
    }

    if (prompt && prompt[0]) {
        subagent_append_message(record->messages, json_build_message("user", prompt));
    } else if (task_id && record->status && strcmp(record->status, "completed") == 0) {
        char *out = subagent_result_json(record);
        subagent_record_free(record);
        cJSON_Delete(json);
        return out;
    } else if (!task_id) {
        subagent_record_free(record);
        cJSON_Delete(json);
        return strdup("Error: 'prompt' argument required for a new subagent");
    }

    char *out = subagent_run(record, cfg);
    subagent_record_free(record);
    cJSON_Delete(json);
    return out;
}
