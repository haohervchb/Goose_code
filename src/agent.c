#include "agent.h"
#include "prompt.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include "util/terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

Agent *agent_init(const char *working_dir) {
    Agent *agent = calloc(1, sizeof(*agent));
    agent->config = config_load();
    if (working_dir) {
        free(agent->config.working_dir);
        agent->config.working_dir = strdup(working_dir);
    }

    agent->api_cfg.base_url = agent->config.base_url;
    agent->api_cfg.api_key = agent->config.api_key;
    agent->api_cfg.model = agent->config.model;
    agent->api_cfg.max_tokens = agent->config.max_tokens;
    agent->api_cfg.temperature = agent->config.temperature;
    agent->api_cfg.max_retries = 3;

    agent->session = session_new();
    agent->tools = tool_registry_init();
    tool_registry_register_all(&agent->tools);
    agent->commands = command_registry_init();
    command_registry_register_all(&agent->commands);

    char *sys_prompt = prompt_build_system(&agent->config, agent->session, agent->config.working_dir);
    agent->system_message = json_build_message("system", sys_prompt);
    free(sys_prompt);

    agent->running = 1;
    return agent;
}

void agent_free(Agent *agent) {
    if (!agent) return;
    session_save(agent->config.session_dir, agent->session);
    session_free(agent->session);
    tool_registry_free(&agent->tools);
    command_registry_free(&agent->commands);
    if (agent->system_message) cJSON_Delete(agent->system_message);
    config_free(&agent->config);
    free(agent);
}

typedef struct {
    ToolRegistry *tools;
    const char *name;
    const char *args;
    const GooseConfig *cfg;
    Session *session;
    char *result;
    PermissionCheckResult perm;
    int is_error;
} ToolExecTask;

static void *tool_exec_thread(void *userdata) {
    ToolExecTask *task = (ToolExecTask *)userdata;
    tool_context_set_session(task->session);
    task->result = tool_registry_execute(task->tools, task->name,
                                          task->args, task->cfg, &task->perm);
    if (task->perm == PERM_CHECK_PROMPT) {
        printf(TERM_YELLOW "\n[Permission required for tool '%s'. Allow? (y/n): " TERM_RESET, task->name);
        fflush(stdout);
        char answer[8];
        if (fgets(answer, sizeof(answer), stdin)) {
            if (answer[0] != 'y' && answer[0] != 'Y') {
                free(task->result);
                task->result = strdup("Tool execution denied by user.");
                task->perm = PERM_CHECK_DENY;
            }
        }
    }
    tool_context_set_session(NULL);
    return NULL;
}

static void agent_refresh_system_message(Agent *agent) {
    if (agent->system_message) {
        cJSON_Delete(agent->system_message);
        agent->system_message = NULL;
    }
    char *sys_prompt = prompt_build_system(&agent->config, agent->session, agent->config.working_dir);
    agent->system_message = json_build_message("system", sys_prompt);
    free(sys_prompt);
}

static void stream_text_cb(const char *text, size_t len, void *ctx) {
    (void)len;
    (void)ctx;
    printf("%s", text);
    fflush(stdout);
}

typedef struct {
    int count;
    char **ids;
    char **names;
    char **args;
} ToolCallCollector;

static void stream_tool_cb(const char *id, const char *name, const char *args, void *ctx) {
    ToolCallCollector *col = (ToolCallCollector *)ctx;
    int idx = col->count++;
    col->ids = realloc(col->ids, col->count * sizeof(char *));
    col->names = realloc(col->names, col->count * sizeof(char *));
    col->args = realloc(col->args, col->count * sizeof(char *));
    col->ids[idx] = strdup(id);
    col->names[idx] = strdup(name);
    col->args[idx] = args ? strdup(args) : strdup("{}");
    printf("\n[Tool call: %s(%s)]\n", name, args ? args : "");
}

static void stream_done_cb(void *ctx) {
    (void)ctx;
}

static void collector_free(ToolCallCollector *col) {
    for (int i = 0; i < col->count; i++) {
        free(col->ids[i]);
        free(col->names[i]);
        free(col->args[i]);
    }
    free(col->ids);
    free(col->names);
    free(col->args);
    col->ids = NULL;
    col->names = NULL;
    col->args = NULL;
    col->count = 0;
}

static int execute_tools_parallel(Agent *agent, ToolCallCollector *calls,
                                   cJSON **tool_results_out) {
    if (calls->count == 0) return 0;

    cJSON *results = cJSON_CreateArray();
    int n = calls->count;

    ToolExecTask *tasks = calloc(n, sizeof(ToolExecTask));
    pthread_t *threads = calloc(n, sizeof(pthread_t));
    int *is_readonly = calloc(n, sizeof(int));

    for (int i = 0; i < n; i++) {
        tasks[i].tools = &agent->tools;
        tasks[i].name = calls->names[i];
        tasks[i].args = calls->args[i];
        tasks[i].cfg = &agent->config;
        tasks[i].session = agent->session;
        tasks[i].result = NULL;
        tasks[i].is_error = 0;
        is_readonly[i] = 0;

        Tool *t = tool_registry_find(&agent->tools, calls->names[i]);
        if (t && t->is_read_only) is_readonly[i] = 1;
    }

    int launched = 0;
    for (int i = 0; i < n; i++) {
        if (is_readonly[i] && launched < 10) {
            if (pthread_create(&threads[i], NULL, tool_exec_thread, &tasks[i]) == 0) {
                launched++;
                continue;
            }
        }
        tool_exec_thread(&tasks[i]);
    }

    for (int i = 0; i < n; i++) {
        if (is_readonly[i] && launched > 0) {
            pthread_join(threads[i], NULL);
        }
    }

    for (int i = 0; i < n; i++) {
        cJSON *result_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(result_obj, "tool_call_id", calls->ids[i]);
        cJSON_AddStringToObject(result_obj, "tool_name", calls->names[i]);

        if (tasks[i].result == NULL) {
            tasks[i].result = strdup("Error: tool returned no result");
            tasks[i].is_error = 1;
        }
        if (tasks[i].perm == PERM_CHECK_BLOCK || tasks[i].perm == PERM_CHECK_DENY) {
            tasks[i].is_error = 1;
        }
        if (strstr(tasks[i].result, "Error:") == tasks[i].result) {
            tasks[i].is_error = 1;
        }

        cJSON_AddBoolToObject(result_obj, "is_error", tasks[i].is_error);
        cJSON_AddStringToObject(result_obj, "content", tasks[i].result);
        cJSON_AddItemToArray(results, result_obj);

        printf(TERM_CYAN "\n[Tool result: %s%s]\n" TERM_RESET,
               calls->names[i], tasks[i].is_error ? " (error)" : "");
        session_add_tool_result(agent->session, calls->ids[i], tasks[i].result);
        free(tasks[i].result);
    }

    *tool_results_out = results;
    free(tasks);
    free(threads);
    free(is_readonly);
    return 0;
}

int agent_run_turn(Agent *agent, const char *user_input) {
    cJSON *user_msg = prompt_build_user_message(user_input);
    session_add_message(agent->session, user_msg);
    cJSON_Delete(user_msg);

    cJSON *tools_def = tool_registry_get_definitions(&agent->tools, &agent->config);

    int turn = 0;
    int max_turns = agent->config.max_turns;
    int retry_count = 0;
    int max_retries = 2;

    while (turn < max_turns && agent->running) {
        if (turn > 0) {
            printf(TERM_DIM "\n--- Turn %d ---\n" TERM_RESET, turn + 1);
        }

        if (session_needs_compact(agent->session, agent->config.context_window)) {
            char *summary = session_compact(agent->session, 10);
            if (summary) {
                printf(TERM_DIM "[Context compacted before API call]\n" TERM_RESET);
                free(summary);
            }
        }

        agent_refresh_system_message(agent);

        cJSON *messages = prompt_build_messages_with_tools(
            agent->system_message, agent->session->messages, NULL);

        printf(TERM_GREEN "assistant: " TERM_RESET);
        fflush(stdout);

        ToolCallCollector calls = {0, NULL, NULL, NULL};
        ApiStreamCallbacks cbs = {stream_text_cb, stream_tool_cb, stream_done_cb, &calls};
        ApiResponse resp;
        ApiStatus status = api_chat_completions(&agent->api_cfg, messages, tools_def, 1, &cbs, &resp);
        cJSON_Delete(messages);

        if (status != API_OK) {
            printf("\n" TERM_RED "Error: %s" TERM_RESET "\n", api_status_str(status));
            if (resp.error) printf("  %s\n", resp.error);

            if (retry_count < max_retries && (status == API_ERROR_RATE_LIMIT || status == API_ERROR_SERVER)) {
                retry_count++;
                int delay = 500 * (1 << (retry_count - 1));
                if (delay > 8000) delay = 8000;
                printf(TERM_YELLOW "Retrying in %dms (attempt %d/%d)...\n" TERM_RESET, delay, retry_count, max_retries);
                usleep(delay * 1000);
                collector_free(&calls);
                api_response_free(&resp);
                continue;
            }

            collector_free(&calls);
            api_response_free(&resp);
            cJSON_Delete(tools_def);
            return -1;
        }

        retry_count = 0;

        if (resp.input_tokens > 0 || resp.output_tokens > 0) {
            agent->session->total_input_tokens += resp.input_tokens;
            agent->session->total_output_tokens += resp.output_tokens;
        }

        if (resp.finish_reason_stop) {
            collector_free(&calls);
            api_response_free(&resp);
            printf("\n");
            break;
        }

        if (calls.count > 0) {
            cJSON *tc_array = cJSON_CreateArray();
            for (int i = 0; i < calls.count; i++) {
                cJSON *tc = cJSON_CreateObject();
                cJSON_AddStringToObject(tc, "id", calls.ids[i]);
                cJSON_AddStringToObject(tc, "type", "function");
                cJSON *fn = cJSON_CreateObject();
                cJSON_AddStringToObject(fn, "name", calls.names[i]);
                cJSON_AddStringToObject(fn, "arguments", calls.args[i]);
                cJSON_AddItemToObject(tc, "function", fn);
                cJSON_AddItemToArray(tc_array, tc);
            }

            cJSON *asst_msg = json_build_tool_message("assistant", tc_array);
            session_add_message(agent->session, asst_msg);
            cJSON_Delete(asst_msg);

            cJSON *tool_results = NULL;
            execute_tools_parallel(agent, &calls, &tool_results);
            if (tool_results) cJSON_Delete(tool_results);
        } else {
            break;
        }

        collector_free(&calls);
        api_response_free(&resp);
        turn++;
    }

    cJSON_Delete(tools_def);
    return 0;
}

int agent_run_repl(Agent *agent) {
    term_print_banner();

    while (agent->running) {
        printf(TERM_BOLD "goosecode> " TERM_RESET);
        fflush(stdout);

        char *input = term_read_line("");
        if (!input) {
            printf("\n");
            agent->running = 0;
            break;
        }

        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        if (input[0] == '/') {
            char *cmd = input + 1;
            char *space = strchr(cmd, ' ');
            char cmd_name[64] = {0};
            char *cmd_args = NULL;

            if (space) {
                size_t nlen = (size_t)(space - cmd);
                if (nlen >= sizeof(cmd_name)) nlen = sizeof(cmd_name) - 1;
                strncpy(cmd_name, cmd, nlen);
                cmd_args = space + 1;
            } else {
                strncpy(cmd_name, cmd, sizeof(cmd_name) - 1);
            }

            if (strcmp(cmd_name, "exit") == 0 || strcmp(cmd_name, "quit") == 0) {
                char *result = command_registry_execute(&agent->commands, "exit", NULL, &agent->config, agent->session);
                if (result && result[0]) printf("%s", result);
                free(result);
                free(input);
                agent->running = 0;
                break;
            }

            char *result = command_registry_execute(&agent->commands, cmd_name, cmd_args, &agent->config, agent->session);
            if (result && result[0]) printf("%s\n", result);
            free(result);
        } else {
            int rc = agent_run_turn(agent, input);
            if (rc != 0) {
                printf(TERM_RED "Turn failed. Type /exit to quit.\n" TERM_RESET);
            }
        }

        free(input);
    }

    return 0;
}

char *agent_process_command(Agent *agent, const char *cmd_name, const char *cmd_args) {
    return command_registry_execute(&agent->commands, cmd_name, cmd_args, &agent->config, agent->session);
}
