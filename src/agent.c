#include "agent.h"
#include "tools/bash_security.h"
#include "util/early_input.h"
#include "compact.h"
#include "prompt.h"
#include "prompt_sections.h"
#include "session_memory.h"
#include "tool_result_store.h"
#include "util/http.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include "util/terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

static Agent *g_current_agent = NULL;

Agent *agent_init(const char *working_dir) {
    Agent *agent = calloc(1, sizeof(*agent));
    g_current_agent = agent;
    bash_security_init();
    early_input_init();
    prompt_sections_clear_cache();
    agent->config = config_load();
    if (working_dir) {
        free(agent->config.working_dir);
        agent->config.working_dir = strdup(working_dir);
    }
    
    if (agent->config.base_url) {
        http_preconnect(agent->config.base_url);
    }

    agent->api_cfg.base_url = agent->config.base_url;
    agent->api_cfg.api_key = agent->config.api_key;
    agent->api_cfg.model = agent->config.model;
    agent->api_cfg.max_tokens = agent->config.max_tokens;
    agent->api_cfg.temperature = agent->config.temperature;
    agent->api_cfg.max_retries = 3;

    agent->session = session_new();
    session_memory_ensure(&agent->config, agent->session);
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
    if (g_current_agent == agent) g_current_agent = NULL;
    session_save(agent->config.session_dir, agent->session);
    session_free(agent->session);
    tool_registry_free(&agent->tools);
    command_registry_free(&agent->commands);
    if (agent->system_message) cJSON_Delete(agent->system_message);
    config_free(&agent->config);
    free(agent);
}

Agent *agent_current(void) {
    return g_current_agent;
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
        if (task->cfg && task->cfg->permission_mode == PERM_ALLOW) {
            free(task->result);
            task->result = tool_registry_execute_unchecked(task->tools, task->name, task->args, task->cfg);
            task->perm = PERM_CHECK_ALLOW;
            tool_context_set_session(NULL);
            return NULL;
        }
        printf(TERM_YELLOW "\n[Permission required for tool '%s'. Allow? (y/n): " TERM_RESET, task->name);
        fflush(stdout);
        char answer[8];
        if (fgets(answer, sizeof(answer), stdin)) {
            if (answer[0] == 'y' || answer[0] == 'Y') {
                free(task->result);
                task->result = tool_registry_execute_unchecked(task->tools, task->name, task->args, task->cfg);
                task->perm = PERM_CHECK_ALLOW;
            } else {
                free(task->result);
                task->result = strdup("Tool execution denied by user.");
                task->perm = PERM_CHECK_DENY;
            }
        }
    }
    tool_context_set_session(NULL);
    return NULL;
}

typedef struct {
    volatile int abort_flag;
    volatile int stop_flag;
    int tty_enabled;
    pthread_t thread;
} TurnInterruptMonitor;

static long monotonic_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void *interrupt_monitor_thread(void *userdata) {
    TurnInterruptMonitor *mon = (TurnInterruptMonitor *)userdata;
    long last_esc = 0;

    term_init();
    while (!mon->stop_flag && !mon->abort_flag) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n != 1) continue;

        if (ch == 27) {
            long now = monotonic_millis();
            if (last_esc > 0 && now - last_esc <= 500) {
                mon->abort_flag = 1;
                break;
            }
            last_esc = now;
        } else {
            last_esc = 0;
        }
    }
    term_restore();
    return NULL;
}

static void interrupt_monitor_start(TurnInterruptMonitor *mon) {
    memset(mon, 0, sizeof(*mon));
    mon->tty_enabled = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (!mon->tty_enabled) return;
    pthread_create(&mon->thread, NULL, interrupt_monitor_thread, mon);
}

static void interrupt_monitor_stop(TurnInterruptMonitor *mon) {
    if (!mon->tty_enabled) return;
    mon->stop_flag = 1;
    pthread_join(mon->thread, NULL);
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

static void agent_refresh_api_config(Agent *agent) {
    agent->api_cfg.base_url = agent->config.base_url;
    agent->api_cfg.api_key = agent->config.api_key;
    agent->api_cfg.model = agent->config.model;
    agent->api_cfg.max_tokens = agent->config.max_tokens;
    agent->api_cfg.temperature = agent->config.temperature;
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
    term_print_tool_call(name, args ? args : "");
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

static int tool_result_is_error_payload(const char *result) {
    if (!result) return 1;
    if (strstr(result, "Error:") == result) return 1;

    cJSON *json = cJSON_Parse(result);
    if (!json) return 0;

    int is_error = 0;
    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (ok && cJSON_IsBool(ok) && !cJSON_IsTrue(ok)) {
        is_error = 1;
    } else {
        cJSON *error = cJSON_GetObjectItem(json, "error");
        if (error && cJSON_IsString(error) && error->valuestring && error->valuestring[0]) {
            is_error = 1;
        }
    }

    cJSON_Delete(json);
    return is_error;
}

static char *collect_multiline_command(const char *first_line, const char *prompt) {
    StrBuf out = strbuf_new();
    if (first_line && first_line[0]) {
        strbuf_append(&out, first_line);
    }

    while (1) {
        char *line = term_read_line_opts(prompt, 0, 0);
        if (!line) break;
        if (strcmp(line, ".") == 0) {
            free(line);
            break;
        }
        if (out.len > 0) strbuf_append_char(&out, '\n');
        strbuf_append(&out, line);
        free(line);
    }

    return strbuf_detach(&out);
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

    char **raw_results = calloc((size_t)n, sizeof(char *));
    for (int i = 0; i < n; i++) {
        raw_results[i] = tasks[i].result;
    }
    char **prepared_results = tool_result_store_prepare_batch(&agent->config, agent->session,
                                                              (const char **)calls->ids,
                                                              raw_results, n);
    free(raw_results);

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
        if (tool_result_is_error_payload(tasks[i].result)) {
            tasks[i].is_error = 1;
        }

        cJSON_AddBoolToObject(result_obj, "is_error", tasks[i].is_error);
        cJSON_AddStringToObject(result_obj, "content", prepared_results[i]);
        cJSON_AddItemToArray(results, result_obj);

        term_print_tool_result(calls->names[i], tasks[i].is_error);
        session_add_tool_result(agent->session, &agent->config, calls->ids[i], prepared_results[i]);
        free(prepared_results[i]);
        free(tasks[i].result);
    }

    *tool_results_out = results;
    free(prepared_results);
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
            char label[32];
            snprintf(label, sizeof(label), "turn %d", turn + 1);
            term_print_block_header(label, TERM_DIM);
        }

        if (session_needs_compact(agent->session, agent->config.context_window)) {
            if (session_compact_circuit_open(agent->session)) {
                printf(TERM_YELLOW "[Context compaction circuit breaker open - too many failures, skipping compaction]\n" TERM_RESET);
            } else {
                int total = cJSON_GetArraySize(agent->session->messages);
                int keep_recent = 10;
                int compact_to = total - keep_recent;
                cJSON *prefix_messages = cJSON_CreateArray();
                for (int i = 0; i < compact_to && i < total; i++) {
                    cJSON *item = cJSON_GetArrayItem(agent->session->messages, i);
                    if (item) cJSON_AddItemToArray(prefix_messages, cJSON_Duplicate(item, 1));
                }
                char *summary = compact_generate_partial_summary(&agent->api_cfg, prefix_messages, COMPACT_PARTIAL_UP_TO);
                cJSON_Delete(prefix_messages);
                if (summary) {
                    session_record_compact_success(agent->session);
                    session_apply_compact_summary(agent->session, keep_recent, summary);
                    printf(TERM_DIM "[Context compacted before API call]\n" TERM_RESET);
                    prompt_sections_clear_cache();
                    free(summary);
                } else {
                    char *fallback = session_compact(agent->session, keep_recent);
                    if (fallback) {
                        session_record_compact_success(agent->session);
                        printf(TERM_DIM "[Context compacted with fallback summary]\n" TERM_RESET);
                        prompt_sections_clear_cache();
                        free(fallback);
                    } else {
                        session_record_compact_failure(agent->session);
                    }
                }
            }
        }

        agent_refresh_system_message(agent);

        cJSON *messages = prompt_build_messages_with_tools(
            agent->system_message, agent->session->messages, NULL);

        term_print_block_header("assistant", TERM_GREEN);

        ToolCallCollector calls = {0, NULL, NULL, NULL};
        TurnInterruptMonitor interrupt_mon;
        interrupt_monitor_start(&interrupt_mon);
        ApiStreamCallbacks cbs = {stream_text_cb, stream_tool_cb, stream_done_cb, &calls, &interrupt_mon.abort_flag};
        ApiResponse resp;
        ApiStatus status = api_chat_completions(&agent->api_cfg, messages, tools_def, 1, &cbs, &resp);
        interrupt_monitor_stop(&interrupt_mon);
        cJSON_Delete(messages);

        if (status != API_OK) {
            if (status == API_ERROR_INTERRUPTED) {
                printf("\n" TERM_YELLOW "[Interrupted]" TERM_RESET "\n");
                collector_free(&calls);
                api_response_free(&resp);
                cJSON_Delete(tools_def);
                return 0;
            }
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
            if (resp.text_content.data && resp.text_content.len > 0) {
                cJSON *assistant_msg = json_build_message("assistant", resp.text_content.data);
                session_add_message(agent->session, assistant_msg);
                cJSON_Delete(assistant_msg);
            }
            collector_free(&calls);
            api_response_free(&resp);
            session_memory_update(&agent->config, agent->session, &agent->api_cfg);
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
            if (resp.text_content.data && resp.text_content.len > 0) {
                cJSON *assistant_msg = json_build_message("assistant", resp.text_content.data);
                session_add_message(agent->session, assistant_msg);
                cJSON_Delete(assistant_msg);
            }
            break;
        }

        collector_free(&calls);
        api_response_free(&resp);
        turn++;
    }

    session_memory_update(&agent->config, agent->session, &agent->api_cfg);
    cJSON_Delete(tools_def);
    return 0;
}

int agent_run_repl(Agent *agent) {
    term_print_banner();
    const ProviderProfile *profile = provider_profile_detect(&agent->config);
    printf(TERM_DIM "  provider: %s | model: %s | base: %s\n\n" TERM_RESET,
           profile ? profile->name : "unknown",
           agent->config.model ? agent->config.model : "(none)",
           agent->config.base_url ? agent->config.base_url : "(none)");

    while (agent->running) {
        char *prompt = term_format_prompt(agent->config.working_dir, agent->session && agent->session->plan_mode);
        char *input = term_read_line_opts(prompt, 1, 1);
        free(prompt);
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

            char *owned_cmd_args = NULL;
            if (strcmp(cmd_name, "plan") == 0 && cmd_args && strcmp(cmd_args, "set") == 0) {
                printf(TERM_DIM "Enter plan text. Finish with a single '.' on its own line.\n" TERM_RESET);
                char *plan_text = collect_multiline_command(NULL, "plan> ");
                StrBuf full_args = strbuf_from("set ");
                strbuf_append(&full_args, plan_text);
                owned_cmd_args = strbuf_detach(&full_args);
                free(plan_text);
                cmd_args = owned_cmd_args;
            }

            if (strcmp(cmd_name, "exit") == 0 || strcmp(cmd_name, "quit") == 0) {
                char *result = command_registry_execute(&agent->commands, "exit", NULL, &agent->config, agent->session);
                if (result && result[0]) printf("%s", result);
                free(result);
                free(owned_cmd_args);
                free(input);
                agent->running = 0;
                break;
            }

            char *result = command_registry_execute(&agent->commands, cmd_name, cmd_args, &agent->config, agent->session);
            if (result && result[0]) printf("%s\n", result);
            free(result);
            agent_refresh_api_config(agent);
            prompt_sections_clear_cache();
            tool_schema_cache_clear();
            free(owned_cmd_args);
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
