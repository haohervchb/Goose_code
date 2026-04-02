#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/util/strbuf.h"
#include "../src/util/json_util.h"
#include "../src/util/sse.h"
#include "../src/permissions.h"
#include "../src/config.h"
#include "../src/session.h"
#include "../src/prompt.h"
#include "../src/tools/tools.h"
#include "../src/tools/subagent_store.h"
#include "../src/commands/commands.h"

static int tests_run = 0;
static int tests_passed = 0;

void test_strbuf_basic(void) {
    tests_run++;
    StrBuf sb = strbuf_new();
    strbuf_append(&sb, "hello");
    assert(strcmp(sb.data, "hello") == 0);
    assert(sb.len == 5);
    strbuf_free(&sb);
    tests_passed++;
    printf("  PASS: test_strbuf_basic\n");
}

void test_strbuf_fmt(void) {
    tests_run++;
    StrBuf sb = strbuf_new();
    strbuf_append_fmt(&sb, "%d + %d = %d", 1, 2, 3);
    assert(strcmp(sb.data, "1 + 2 = 3") == 0);
    strbuf_free(&sb);
    tests_passed++;
    printf("  PASS: test_strbuf_fmt\n");
}

void test_strbuf_trim(void) {
    tests_run++;
    StrBuf sb = strbuf_from("  hello world  ");
    strbuf_trim(&sb);
    assert(strcmp(sb.data, "hello world") == 0);
    strbuf_free(&sb);
    tests_passed++;
    printf("  PASS: test_strbuf_trim\n");
}

void test_json_build_message(void) {
    tests_run++;
    cJSON *msg = json_build_message("user", "hello");
    assert(msg != NULL);
    const char *role = json_get_string(msg, "role");
    assert(role != NULL && strcmp(role, "user") == 0);
    const char *content = json_get_string(msg, "content");
    assert(content != NULL && strcmp(content, "hello") == 0);
    cJSON_Delete(msg);
    tests_passed++;
    printf("  PASS: test_json_build_message\n");
}

void test_json_tool_def(void) {
    tests_run++;
    cJSON *params = cJSON_Parse("{\"type\":\"object\",\"properties\":{}}");
    cJSON *def = json_build_tool_def("bash", "Run commands", params);
    assert(def != NULL);
    cJSON *fn = json_get_object(def, "function");
    assert(fn != NULL);
    assert(strcmp(json_get_string(fn, "name"), "bash") == 0);
    cJSON_Delete(def);
    tests_passed++;
    printf("  PASS: test_json_tool_def\n");
}

void test_perm_read_only(void) {
    tests_run++;
    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_READ_ONLY;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    PermissionCheckResult r = permissions_check(&cfg, "read_file", "{}", PERM_READ_ONLY);
    assert(r == PERM_CHECK_ALLOW);

    r = permissions_check(&cfg, "bash", "{}", PERM_WORKSPACE_WRITE);
    assert(r == PERM_CHECK_BLOCK);

    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);
    tests_passed++;
    printf("  PASS: test_perm_read_only\n");
}

void test_perm_allow(void) {
    tests_run++;
    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    PermissionCheckResult r = permissions_check(&cfg, "bash", "{}", PERM_WORKSPACE_WRITE);
    assert(r == PERM_CHECK_ALLOW);

    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);
    tests_passed++;
    printf("  PASS: test_perm_allow\n");
}

void test_perm_deny_list(void) {
    tests_run++;
    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();
    cJSON_AddItemToArray(cfg.denied_tools, cJSON_CreateString("bash"));

    PermissionCheckResult r = permissions_check(&cfg, "bash", "{}", PERM_WORKSPACE_WRITE);
    assert(r == PERM_CHECK_BLOCK);

    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);
    tests_passed++;
    printf("  PASS: test_perm_deny_list\n");
}

void test_config_perm_mode_str(void) {
    tests_run++;
    assert(strcmp(config_perm_mode_str(PERM_READ_ONLY), "read-only") == 0);
    assert(strcmp(config_perm_mode_str(PERM_ALLOW), "allow") == 0);
    assert(strcmp(config_perm_mode_str(PERM_PROMPT), "prompt") == 0);
    tests_passed++;
    printf("  PASS: test_config_perm_mode_str\n");
}

void test_config_perm_mode_from_str(void) {
    tests_run++;
    assert(config_perm_mode_from_str("read-only") == PERM_READ_ONLY);
    assert(config_perm_mode_from_str("allow") == PERM_ALLOW);
    assert(config_perm_mode_from_str("unknown") == PERM_PROMPT);
    tests_passed++;
    printf("  PASS: test_config_perm_mode_from_str\n");
}

void test_sse_multi_tool_calls_by_index(void) {
    tests_run++;

    SseParser parser;
    sse_parser_init(&parser);

    const char *chunk1 =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"bash\",\"arguments\":\"{\\\"command\\\":\\\"ls -la /tmp\\\"}\"}}]}}]}";
    const char *chunk2 =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"call_2\",\"function\":{\"name\":\"bash\",\"arguments\":\"{\\\"command\\\":\\\"pwd\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}";

    SseEvent ev = sse_parse_line(&parser, chunk1, strlen(chunk1));
    assert(ev.text == NULL && ev.tool_call_id == NULL && ev.finish_reason_tool_calls == 0);
    sse_event_free(&ev);

    ev = sse_parse_line(&parser, "", 0);
    assert(ev.text == NULL && ev.tool_call_id == NULL && ev.finish_reason_tool_calls == 0);
    sse_event_free(&ev);

    ev = sse_parse_line(&parser, chunk2, strlen(chunk2));
    assert(ev.text == NULL && ev.tool_call_id == NULL && ev.finish_reason_tool_calls == 0);
    sse_event_free(&ev);

    ev = sse_parse_line(&parser, "", 0);
    assert(ev.tool_call_id != NULL && strcmp(ev.tool_call_id, "call_1") == 0);
    assert(ev.tool_name != NULL && strcmp(ev.tool_name, "bash") == 0);
    assert(ev.tool_args != NULL && strcmp(ev.tool_args, "{\"command\":\"ls -la /tmp\"}") == 0);
    assert(ev.finish_reason_tool_calls == 1);
    sse_event_free(&ev);

    ev = sse_parser_next_event(&parser);
    assert(ev.tool_call_id != NULL && strcmp(ev.tool_call_id, "call_2") == 0);
    assert(ev.tool_name != NULL && strcmp(ev.tool_name, "bash") == 0);
    assert(ev.tool_args != NULL && strcmp(ev.tool_args, "{\"command\":\"pwd\"}") == 0);
    assert(ev.finish_reason_tool_calls == 1);
    sse_event_free(&ev);

    ev = sse_parser_next_event(&parser);
    assert(ev.text == NULL && ev.tool_call_id == NULL && ev.finish_reason_tool_calls == 0);
    sse_event_free(&ev);

    sse_parser_free(&parser);
    tests_passed++;
    printf("  PASS: test_sse_multi_tool_calls_by_index\n");
}

void test_ask_user_question_single_select(void) {
    tests_run++;

    const char *args =
        "{\"questions\":[{\"question\":\"Pick a color\",\"header\":\"Color\",\"multiple\":false,\"options\":[{\"label\":\"Red\",\"description\":\"Warm\"},{\"label\":\"Blue\",\"description\":\"Cool\"}]}]}";
    FILE *input = fmemopen((void *)"2\n", 2, "r");
    assert(input != NULL);

    char *output_buf = NULL;
    size_t output_len = 0;
    FILE *output = open_memstream(&output_buf, &output_len);
    assert(output != NULL);

    char *result = tool_execute_ask_user_question_with_io(args, input, output);
    fflush(output);

    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *answers = json_get_array(json, "answers");
    assert(answers != NULL && cJSON_GetArraySize(answers) == 1);
    cJSON *answer = cJSON_GetArrayItem(answers, 0);
    cJSON *selections = json_get_array(answer, "selections");
    assert(selections != NULL && cJSON_GetArraySize(selections) == 1);
    cJSON *selection = cJSON_GetArrayItem(selections, 0);
    assert(cJSON_IsString(selection) && strcmp(selection->valuestring, "Blue") == 0);
    assert(strstr(output_buf, "Pick a color") != NULL);

    cJSON_Delete(json);
    free(result);
    fclose(input);
    fclose(output);
    free(output_buf);

    tests_passed++;
    printf("  PASS: test_ask_user_question_single_select\n");
}

void test_ask_user_question_multiple_and_custom(void) {
    tests_run++;

    const char *args =
        "{\"questions\":[{\"question\":\"Choose toppings\",\"header\":\"Pizza\",\"multiple\":true,\"options\":[{\"label\":\"Cheese\",\"description\":\"Classic\"},{\"label\":\"Mushroom\",\"description\":\"Earthy\"}],\"custom\":true}]}";
    FILE *input = fmemopen((void *)"1, olives\n", 11, "r");
    assert(input != NULL);

    char *output_buf = NULL;
    size_t output_len = 0;
    FILE *output = open_memstream(&output_buf, &output_len);
    assert(output != NULL);

    char *result = tool_execute_ask_user_question_with_io(args, input, output);
    fflush(output);

    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *answers = json_get_array(json, "answers");
    assert(answers != NULL && cJSON_GetArraySize(answers) == 1);
    cJSON *answer = cJSON_GetArrayItem(answers, 0);
    cJSON *selections = json_get_array(answer, "selections");
    assert(selections != NULL && cJSON_GetArraySize(selections) == 2);
    assert(strcmp(cJSON_GetArrayItem(selections, 0)->valuestring, "Cheese") == 0);
    assert(strcmp(cJSON_GetArrayItem(selections, 1)->valuestring, "olives") == 0);

    cJSON_Delete(json);
    free(result);
    fclose(input);
    fclose(output);
    free(output_buf);

    tests_passed++;
    printf("  PASS: test_ask_user_question_multiple_and_custom\n");
}

void test_ask_user_question_reprompts_invalid_choice(void) {
    tests_run++;

    const char *args =
        "{\"questions\":[{\"question\":\"Pick one\",\"header\":\"Test\",\"multiple\":false,\"custom\":false,\"options\":[{\"label\":\"One\",\"description\":\"First\"},{\"label\":\"Two\",\"description\":\"Second\"}]}]}";
    FILE *input = fmemopen((void *)"9\n2\n", 4, "r");
    assert(input != NULL);

    char *output_buf = NULL;
    size_t output_len = 0;
    FILE *output = open_memstream(&output_buf, &output_len);
    assert(output != NULL);

    char *result = tool_execute_ask_user_question_with_io(args, input, output);
    fflush(output);

    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *answers = json_get_array(json, "answers");
    cJSON *answer = cJSON_GetArrayItem(answers, 0);
    cJSON *selections = json_get_array(answer, "selections");
    assert(strcmp(cJSON_GetArrayItem(selections, 0)->valuestring, "Two") == 0);
    assert(strstr(output_buf, "Choice out of range.") != NULL);

    cJSON_Delete(json);
    free(result);
    fclose(input);
    fclose(output);
    free(output_buf);

    tests_passed++;
    printf("  PASS: test_ask_user_question_reprompts_invalid_choice\n");
}

static int tool_defs_include_name(cJSON *defs, const char *name) {
    cJSON *item;
    cJSON_ArrayForEach(item, defs) {
        cJSON *fn = json_get_object(item, "function");
        const char *tool_name = fn ? json_get_string(fn, "name") : NULL;
        if (tool_name && strcmp(tool_name, name) == 0) return 1;
    }
    return 0;
}

static cJSON *tool_def_for_name(cJSON *defs, const char *name) {
    cJSON *item;
    cJSON_ArrayForEach(item, defs) {
        cJSON *fn = json_get_object(item, "function");
        const char *tool_name = fn ? json_get_string(fn, "name") : NULL;
        if (tool_name && strcmp(tool_name, name) == 0) return item;
    }
    return NULL;
}

static int required_array_contains(cJSON *required, const char *name) {
    cJSON *item;
    cJSON_ArrayForEach(item, required) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, name) == 0) return 1;
    }
    return 0;
}

void test_tool_definitions_hide_blocked_tools(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_READ_ONLY;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);

    cJSON *defs = tool_registry_get_definitions(&reg, &cfg);
    assert(defs != NULL);
    assert(tool_defs_include_name(defs, "read_file"));
    assert(!tool_defs_include_name(defs, "bash"));

    cJSON_Delete(defs);
    tool_registry_free(&reg);
    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);

    tests_passed++;
    printf("  PASS: test_tool_definitions_hide_blocked_tools\n");
}

void test_tool_definitions_respect_allow_and_deny_lists(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_READ_ONLY;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();
    cJSON_AddItemToArray(cfg.allowed_tools, cJSON_CreateString("bash"));
    cJSON_AddItemToArray(cfg.denied_tools, cJSON_CreateString("read_file"));

    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);

    cJSON *defs = tool_registry_get_definitions(&reg, &cfg);
    assert(defs != NULL);
    assert(tool_defs_include_name(defs, "bash"));
    assert(!tool_defs_include_name(defs, "read_file"));

    cJSON_Delete(defs);
    tool_registry_free(&reg);
    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);

    tests_passed++;
    printf("  PASS: test_tool_definitions_respect_allow_and_deny_lists\n");
}

void test_tool_definitions_include_repl_and_powershell_schemas(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);

    cJSON *defs = tool_registry_get_definitions(&reg, &cfg);
    cJSON *repl_def = tool_def_for_name(defs, "repl");
    cJSON *powershell_def = tool_def_for_name(defs, "powershell");
    assert(repl_def != NULL);
    assert(powershell_def != NULL);

    cJSON *repl_fn = json_get_object(repl_def, "function");
    cJSON *repl_params = json_get_object(repl_fn, "parameters");
    cJSON *repl_props = json_get_object(repl_params, "properties");
    cJSON *repl_required = json_get_array(repl_params, "required");
    assert(json_get_object(repl_props, "code") != NULL);
    assert(required_array_contains(repl_required, "code"));

    cJSON *powershell_fn = json_get_object(powershell_def, "function");
    cJSON *powershell_params = json_get_object(powershell_fn, "parameters");
    cJSON *powershell_props = json_get_object(powershell_params, "properties");
    cJSON *powershell_required = json_get_array(powershell_params, "required");
    assert(json_get_object(powershell_props, "command") != NULL);
    assert(required_array_contains(powershell_required, "command"));

    cJSON_Delete(defs);
    tool_registry_free(&reg);
    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);

    tests_passed++;
    printf("  PASS: test_tool_definitions_include_repl_and_powershell_schemas\n");
}

void test_tool_definitions_include_message_and_config_schemas(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);

    cJSON *defs = tool_registry_get_definitions(&reg, &cfg);
    cJSON *send_def = tool_def_for_name(defs, "send_message");
    cJSON *config_def = tool_def_for_name(defs, "config");
    assert(send_def != NULL);
    assert(config_def != NULL);

    cJSON *send_fn = json_get_object(send_def, "function");
    cJSON *send_params = json_get_object(send_fn, "parameters");
    cJSON *send_props = json_get_object(send_params, "properties");
    cJSON *send_required = json_get_array(send_params, "required");
    assert(json_get_object(send_props, "message") != NULL);
    assert(required_array_contains(send_required, "message"));

    cJSON *config_fn = json_get_object(config_def, "function");
    cJSON *config_params = json_get_object(config_fn, "parameters");
    cJSON *config_props = json_get_object(config_params, "properties");
    assert(json_get_object(config_props, "setting") != NULL);
    assert(json_get_object(config_props, "value") != NULL);

    cJSON_Delete(defs);
    tool_registry_free(&reg);
    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);

    tests_passed++;
    printf("  PASS: test_tool_definitions_include_message_and_config_schemas\n");
}

void test_session_plan_persistence(void) {
    tests_run++;

    char session_dir[] = "/tmp/goosecode_plan_session_XXXXXX";
    assert(mkdtemp(session_dir) != NULL);

    Session *sess = session_new();
    session_set_plan_mode(sess, 1);
    session_set_plan(sess, "1. Inspect\n2. Fix\n3. Verify");
    assert(session_save(session_dir, sess) == 0);

    Session *loaded = session_load(session_dir, sess->id);
    assert(loaded != NULL);
    assert(loaded->plan_mode == 1);
    assert(loaded->plan_content != NULL);
    assert(strcmp(loaded->plan_content, "1. Inspect\n2. Fix\n3. Verify") == 0);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.json", session_dir, sess->id);
    remove(path);
    rmdir(session_dir);
    session_free(sess);
    session_free(loaded);

    tests_passed++;
    printf("  PASS: test_session_plan_persistence\n");
}

void test_prompt_includes_plan_mode(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.model = strdup("test-model");
    Session *sess = session_new();
    session_set_plan_mode(sess, 1);
    session_set_plan(sess, "1. Draft\n2. Review");

    char *prompt = prompt_build_system(&cfg, sess, "/home/rah/goosecode");
    assert(prompt != NULL);
    assert(strstr(prompt, "## Plan Mode") != NULL);
    assert(strstr(prompt, "1. Draft") != NULL);

    free(prompt);
    free(cfg.model);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_prompt_includes_plan_mode\n");
}

void test_plan_mode_tools_toggle_session(void) {
    tests_run++;

    GooseConfig cfg = {0};
    Session *sess = session_new();
    tool_context_set_session(sess);

    char *result = tool_execute_enter_plan_mode("{\"plan\":\"1. Test\n2. Confirm\"}", &cfg);
    assert(result != NULL);
    assert(sess->plan_mode == 1);
    assert(sess->plan_content != NULL);
    assert(strstr(result, "Plan mode enabled") != NULL);
    free(result);

    result = tool_execute_exit_plan_mode("{}", &cfg);
    assert(result != NULL);
    assert(sess->plan_mode == 0);
    assert(strstr(result, "Plan mode disabled") != NULL);
    free(result);

    tool_context_set_session(NULL);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_plan_mode_tools_toggle_session\n");
}

void test_plan_command_updates_session(void) {
    tests_run++;

    GooseConfig cfg = {0};
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "plan", "set 1. Check\n2. Ship", &cfg, sess);
    assert(result != NULL);
    assert(sess->plan_mode == 1);
    assert(sess->plan_content != NULL);
    assert(strcmp(sess->plan_content, "1. Check\n2. Ship") == 0);
    free(result);

    result = command_registry_execute(&reg, "plan", "off", &cfg, sess);
    assert(result != NULL);
    assert(sess->plan_mode == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_plan_command_updates_session\n");
}

void test_todo_write_persists_and_formats(void) {
    tests_run++;

    char todo_path[] = "/tmp/goosecode_todos_XXXXXX.json";
    int fd = mkstemps(todo_path, 5);
    assert(fd != -1);
    close(fd);

    GooseConfig cfg = {0};
    cfg.todo_store = todo_path;

    const char *args =
        "{\"todos\":[{\"content\":\"Investigate crash\",\"status\":\"in_progress\"},{\"content\":\"Add regression test\",\"status\":\"pending\"}]}";
    char *result = tool_execute_todo_write(args, &cfg);
    assert(result != NULL);
    assert(strstr(result, "Todos updated:") != NULL);
    assert(strstr(result, "[in_progress|medium] Investigate crash") != NULL);
    assert(strstr(result, "[pending|medium] Add regression test") != NULL);

    char *saved = json_read_file(todo_path);
    assert(saved != NULL);
    cJSON *saved_json = cJSON_Parse(saved);
    assert(saved_json != NULL);
    assert(cJSON_IsArray(saved_json));
    assert(cJSON_GetArraySize(saved_json) == 2);
    cJSON *first = cJSON_GetArrayItem(saved_json, 0);
    assert(strcmp(json_get_string(first, "content"), "Investigate crash") == 0);
    assert(strcmp(json_get_string(first, "id"), "task_1") == 0);
    assert(strcmp(json_get_string(first, "priority"), "medium") == 0);

    cJSON_Delete(saved_json);
    free(saved);
    free(result);
    remove(todo_path);

    tests_passed++;
    printf("  PASS: test_todo_write_persists_and_formats\n");
}

void test_todo_write_rejects_multiple_in_progress(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.todo_store = "/tmp/unused_todos.json";

    const char *args =
        "{\"todos\":[{\"content\":\"One\",\"status\":\"in_progress\"},{\"content\":\"Two\",\"status\":\"in_progress\"}]}";
    char *result = tool_execute_todo_write(args, &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: only one task can be 'in_progress' at a time") == 0);
    free(result);

    tests_passed++;
    printf("  PASS: test_todo_write_rejects_multiple_in_progress\n");
}

void test_task_tools_create_get_list_update(void) {
    tests_run++;

    char todo_path[] = "/tmp/goosecode_tasks_XXXXXX.json";
    int fd = mkstemps(todo_path, 5);
    assert(fd != -1);
    close(fd);

    GooseConfig cfg = {0};
    cfg.todo_store = todo_path;

    char *result = tool_execute_task_create(
        "{\"content\":\"Implement task tools\",\"status\":\"in_progress\",\"priority\":\"high\"}",
        &cfg);
    assert(result != NULL);
    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *task = json_get_object(json, "task");
    assert(task != NULL);
    const char *task_id = json_get_string(task, "id");
    assert(task_id != NULL && strcmp(task_id, "task_1") == 0);
    assert(strcmp(json_get_string(task, "status"), "in_progress") == 0);
    assert(strcmp(json_get_string(task, "priority"), "high") == 0);
    cJSON_Delete(json);
    free(result);

    result = tool_execute_task_get("{\"task_id\":\"task_1\"}", &cfg);
    json = cJSON_Parse(result);
    assert(json != NULL);
    task = json_get_object(json, "task");
    assert(task != NULL);
    assert(strcmp(json_get_string(task, "content"), "Implement task tools") == 0);
    cJSON_Delete(json);
    free(result);

    result = tool_execute_task_list("{}", &cfg);
    json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *tasks = json_get_array(json, "tasks");
    assert(tasks != NULL && cJSON_GetArraySize(tasks) == 1);
    task = cJSON_GetArrayItem(tasks, 0);
    assert(strcmp(json_get_string(task, "id"), "task_1") == 0);
    cJSON_Delete(json);
    free(result);

    result = tool_execute_task_update(
        "{\"task_id\":\"task_1\",\"status\":\"completed\",\"priority\":\"low\"}",
        &cfg);
    json = cJSON_Parse(result);
    assert(json != NULL);
    task = json_get_object(json, "task");
    assert(task != NULL);
    assert(strcmp(json_get_string(task, "status"), "completed") == 0);
    assert(strcmp(json_get_string(task, "priority"), "low") == 0);
    cJSON_Delete(json);
    free(result);

    remove(todo_path);

    tests_passed++;
    printf("  PASS: test_task_tools_create_get_list_update\n");
}

void test_task_tools_see_todo_write_entries(void) {
    tests_run++;

    char todo_path[] = "/tmp/goosecode_tasks_compat_XXXXXX.json";
    int fd = mkstemps(todo_path, 5);
    assert(fd != -1);
    close(fd);

    GooseConfig cfg = {0};
    cfg.todo_store = todo_path;

    char *result = tool_execute_todo_write(
        "{\"todos\":[{\"content\":\"Legacy todo\",\"status\":\"pending\"}]}",
        &cfg);
    assert(result != NULL);
    free(result);

    result = tool_execute_task_list("{}", &cfg);
    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    cJSON *tasks = json_get_array(json, "tasks");
    assert(tasks != NULL && cJSON_GetArraySize(tasks) == 1);
    cJSON *task = cJSON_GetArrayItem(tasks, 0);
    assert(strcmp(json_get_string(task, "content"), "Legacy todo") == 0);
    assert(strcmp(json_get_string(task, "id"), "task_1") == 0);
    cJSON_Delete(json);
    free(result);
    remove(todo_path);

    tests_passed++;
    printf("  PASS: test_task_tools_see_todo_write_entries\n");
}

void test_task_update_rejects_unknown_task(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.todo_store = "/tmp/goosecode_missing_task.json";

    char *result = tool_execute_task_update(
        "{\"task_id\":\"task_999\",\"status\":\"completed\"}",
        &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: task not found") == 0);
    free(result);

    tests_passed++;
    printf("  PASS: test_task_update_rejects_unknown_task\n");
}

void test_subagent_record_persistence(void) {
    tests_run++;

    char subagent_dir[] = "/tmp/goosecode_subagents_XXXXXX";
    assert(mkdtemp(subagent_dir) != NULL);

    GooseConfig cfg = {0};
    cfg.subagent_dir = subagent_dir;

    SubagentRecord *record = subagent_record_new("subagent_test_1");
    free(record->description);
    record->description = strdup("Explore the repository");
    free(record->subagent_type);
    record->subagent_type = strdup("explore");
    free(record->model);
    record->model = strdup("test-model");
    free(record->working_dir);
    record->working_dir = strdup("/tmp/work");
    free(record->workspace_mode);
    record->workspace_mode = strdup("git_worktree");
    free(record->result);
    record->result = strdup("done");
    cJSON_AddItemToArray(record->messages, json_build_message("user", "inspect files"));

    char *err = subagent_record_save(&cfg, record);
    assert(err == NULL);

    SubagentRecord *loaded = subagent_record_load(&cfg, "subagent_test_1");
    assert(loaded != NULL);
    assert(strcmp(loaded->description, "Explore the repository") == 0);
    assert(strcmp(loaded->subagent_type, "explore") == 0);
    assert(strcmp(loaded->workspace_mode, "git_worktree") == 0);
    assert(strcmp(loaded->result, "done") == 0);
    assert(cJSON_GetArraySize(loaded->messages) == 1);

    char path[1024];
    snprintf(path, sizeof(path), "%s/subagent_test_1.json", subagent_dir);
    remove(path);
    rmdir(subagent_dir);
    subagent_record_free(record);
    subagent_record_free(loaded);

    tests_passed++;
    printf("  PASS: test_subagent_record_persistence\n");
}

void test_agent_tool_schema_includes_task_id(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    ToolRegistry reg = tool_registry_init();
    tool_registry_register_all(&reg);
    cJSON *defs = tool_registry_get_definitions(&reg, &cfg);
    cJSON *agent_def = tool_def_for_name(defs, "agent");
    assert(agent_def != NULL);
    cJSON *fn = json_get_object(agent_def, "function");
    cJSON *params = json_get_object(fn, "parameters");
    cJSON *props = json_get_object(params, "properties");
    assert(json_get_object(props, "task_id") != NULL);
    assert(json_get_object(props, "working_dir") != NULL);
    assert(json_get_object(props, "use_worktree") != NULL);

    cJSON_Delete(defs);
    tool_registry_free(&reg);
    cJSON_Delete(cfg.allowed_tools);
    cJSON_Delete(cfg.denied_tools);

    tests_passed++;
    printf("  PASS: test_agent_tool_schema_includes_task_id\n");
}

void test_agent_tool_rejects_unknown_subagent_type(void) {
    tests_run++;

    GooseConfig cfg = {0};
    char *result = tool_execute_agent_tool(
        "{\"prompt\":\"test\",\"description\":\"test\",\"subagent_type\":\"bogus\"}",
        &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: subagent_type must be one of general, explore, or plan") == 0);
    free(result);

    tests_passed++;
    printf("  PASS: test_agent_tool_rejects_unknown_subagent_type\n");
}

void test_agent_tool_rejects_missing_resume_id(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.subagent_dir = "/tmp/missing_subagent_dir";
    char *result = tool_execute_agent_tool("{\"task_id\":\"subagent_missing\"}", &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: subagent task_id not found") == 0);
    free(result);

    tests_passed++;
    printf("  PASS: test_agent_tool_rejects_missing_resume_id\n");
}

void test_mcp_list_and_read_resources(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.mcp_servers = cJSON_CreateArray();
    cJSON *server = cJSON_CreateObject();
    cJSON_AddStringToObject(server, "name", "test");
    cJSON_AddStringToObject(server, "command", "/usr/bin/python3");
    cJSON *args = cJSON_CreateArray();
    cJSON_AddItemToArray(args, cJSON_CreateString("/home/rah/goosecode/tests/mcp_test_server.py"));
    cJSON_AddItemToObject(server, "args", args);
    cJSON_AddItemToArray(cfg.mcp_servers, server);

    char *result = tool_execute_list_mcp_resources("{\"server\":\"test\"}", &cfg);
    assert(result != NULL);
    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    assert(cJSON_IsArray(json));
    assert(cJSON_GetArraySize(json) == 1);
    cJSON *resource = cJSON_GetArrayItem(json, 0);
    assert(strcmp(json_get_string(resource, "uri"), "memo://alpha") == 0);
    cJSON_Delete(json);
    free(result);

    result = tool_execute_read_mcp_resource("{\"server\":\"test\",\"uri\":\"memo://alpha\"}", &cfg);
    assert(result != NULL);
    json = cJSON_Parse(result);
    assert(json != NULL);
    assert(cJSON_IsArray(json));
    assert(cJSON_GetArraySize(json) == 1);
    cJSON *content = cJSON_GetArrayItem(json, 0);
    assert(strcmp(json_get_string(content, "text"), "alpha contents") == 0);
    cJSON_Delete(json);
    free(result);

    cJSON_Delete(cfg.mcp_servers);

    tests_passed++;
    printf("  PASS: test_mcp_list_and_read_resources\n");
}

void test_mcp_missing_server_and_resource_errors(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.mcp_servers = cJSON_CreateArray();

    char *result = tool_execute_list_mcp_resources("{\"server\":\"missing\"}", &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: MCP server not found") == 0);
    free(result);

    cJSON *server = cJSON_CreateObject();
    cJSON_AddStringToObject(server, "name", "test");
    cJSON_AddStringToObject(server, "command", "/usr/bin/python3");
    cJSON *args = cJSON_CreateArray();
    cJSON_AddItemToArray(args, cJSON_CreateString("/home/rah/goosecode/tests/mcp_test_server.py"));
    cJSON_AddItemToObject(server, "args", args);
    cJSON_AddItemToArray(cfg.mcp_servers, server);

    result = tool_execute_read_mcp_resource("{\"server\":\"test\",\"uri\":\"memo://missing\"}", &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: Resource not found") == 0);
    free(result);

    cJSON_Delete(cfg.mcp_servers);

    tests_passed++;
    printf("  PASS: test_mcp_missing_server_and_resource_errors\n");
}

void test_lsp_hover_definition_and_symbols(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.working_dir = "/tmp";

    char source_path[] = "/tmp/goosecode_lsp_XXXXXX.rs";
    int fd = mkstemps(source_path, 3);
    assert(fd != -1);
    const char *source = "fn main() {\n    println!(\"hi\");\n}\n";
    assert(write(fd, source, strlen(source)) == (ssize_t)strlen(source));
    close(fd);

    char args_buf[1024];
    snprintf(args_buf, sizeof(args_buf),
             "{\"action\":\"hover\",\"file_path\":\"%s\",\"line\":0,\"character\":3,\"server_command\":\"/usr/bin/python3\",\"server_args\":[\"/home/rah/goosecode/tests/lsp_test_server.py\"]}",
             source_path);
    char *result = tool_execute_lsp(args_buf, &cfg);
    assert(result != NULL);
    cJSON *json = cJSON_Parse(result);
    assert(json != NULL);
    assert(strcmp(json_get_string(json, "action"), "hover") == 0);
    cJSON *hover = json_get_object(json, "result");
    cJSON *contents = json_get_object(hover, "contents");
    assert(strcmp(json_get_string(contents, "value"), "hover information") == 0);
    cJSON_Delete(json);
    free(result);

    snprintf(args_buf, sizeof(args_buf),
             "{\"action\":\"definition\",\"file_path\":\"%s\",\"line\":0,\"character\":3,\"server_command\":\"/usr/bin/python3\",\"server_args\":[\"/home/rah/goosecode/tests/lsp_test_server.py\"]}",
             source_path);
    result = tool_execute_lsp(args_buf, &cfg);
    assert(result != NULL);
    json = cJSON_Parse(result);
    assert(json != NULL);
    assert(strcmp(json_get_string(json, "action"), "definition") == 0);
    cJSON *locations = json_get_array(json, "result");
    assert(locations != NULL && cJSON_GetArraySize(locations) == 1);
    cJSON *location = cJSON_GetArrayItem(locations, 0);
    assert(strcmp(json_get_string(location, "uri"), "file:///tmp/example.rs") == 0);
    cJSON_Delete(json);
    free(result);

    snprintf(args_buf, sizeof(args_buf),
             "{\"action\":\"document_symbols\",\"file_path\":\"%s\",\"server_command\":\"/usr/bin/python3\",\"server_args\":[\"/home/rah/goosecode/tests/lsp_test_server.py\"]}",
             source_path);
    result = tool_execute_lsp(args_buf, &cfg);
    assert(result != NULL);
    json = cJSON_Parse(result);
    assert(json != NULL);
    assert(strcmp(json_get_string(json, "action"), "document_symbols") == 0);
    cJSON *symbols = json_get_array(json, "result");
    assert(symbols != NULL && cJSON_GetArraySize(symbols) == 1);
    cJSON *symbol = cJSON_GetArrayItem(symbols, 0);
    assert(strcmp(json_get_string(symbol, "name"), "main") == 0);
    cJSON_Delete(json);
    free(result);

    remove(source_path);

    tests_passed++;
    printf("  PASS: test_lsp_hover_definition_and_symbols\n");
}

void test_lsp_rejects_unknown_action(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.working_dir = "/tmp";

    char source_path[] = "/tmp/goosecode_lsp_invalid_XXXXXX.rs";
    int fd = mkstemps(source_path, 3);
    assert(fd != -1);
    close(fd);

    char args_buf[1024];
    snprintf(args_buf, sizeof(args_buf),
             "{\"action\":\"bogus\",\"file_path\":\"%s\",\"server_command\":\"/usr/bin/python3\",\"server_args\":[\"/home/rah/goosecode/tests/lsp_test_server.py\"]}",
             source_path);
    char *result = tool_execute_lsp(args_buf, &cfg);
    assert(result != NULL);
    assert(strcmp(result, "Error: action must be one of hover, definition, or document_symbols") == 0);
    free(result);
    remove(source_path);

    tests_passed++;
    printf("  PASS: test_lsp_rejects_unknown_action\n");
}

void test_tasks_command_create_list_show_and_set(void) {
    tests_run++;

    char todo_path[] = "/tmp/goosecode_cmd_tasks_XXXXXX.json";
    int fd = mkstemps(todo_path, 5);
    assert(fd != -1);
    close(fd);

    GooseConfig cfg = {0};
    cfg.todo_store = todo_path;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "tasks", "create Write command test", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Created task:") != NULL);
    assert(strstr(result, "task_1 [pending|medium] Write command test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "tasks", "list", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "task_1 [pending|medium] Write command test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "tasks", "show task_1", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "task_1 [pending|medium] Write command test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "tasks", "set task_1 completed", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Updated task:") != NULL);
    assert(strstr(result, "task_1 [completed|medium] Write command test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "tasks", "completed", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "task_1 [completed|medium] Write command test") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);
    remove(todo_path);

    tests_passed++;
    printf("  PASS: test_tasks_command_create_list_show_and_set\n");
}

void test_tasks_command_rejects_bad_status(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.todo_store = "/tmp/goosecode_cmd_tasks_invalid.json";
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "tasks", "set task_1 bogus", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Error: status must be pending, in_progress, completed, or cancelled\n") == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_tasks_command_rejects_bad_status\n");
}

void test_config_command_show_and_inspect(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.model = "test-model";
    cfg.base_url = "http://localhost:8083/v1";
    cfg.permission_mode = PERM_ALLOW;
    cfg.max_tokens = 4096;
    cfg.max_turns = 12;
    cfg.working_dir = "/tmp/project";

    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "config", "", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Current configuration:") != NULL);
    assert(strstr(result, "model: test-model") != NULL);
    assert(strstr(result, "max_turns: 12") != NULL);
    free(result);

    result = command_registry_execute(&reg, "config", "model", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Current model: test-model\n") == 0);
    free(result);

    result = command_registry_execute(&reg, "config", "max_turns 24", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Max turns set to: 24 (runtime only)\n") == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_config_command_show_and_inspect\n");
}

void test_config_command_rejects_unknown_setting(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.model = "test-model";
    cfg.base_url = "http://localhost:8083/v1";
    cfg.permission_mode = PERM_ALLOW;
    cfg.max_tokens = 4096;
    cfg.max_turns = 12;
    cfg.working_dir = "/tmp/project";

    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "config", "bogus", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Unknown setting: bogus") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_config_command_rejects_unknown_setting\n");
}

void test_branch_command_show_list_create_and_switch(void) {
    tests_run++;

    char repo_dir[] = "/tmp/goosecode_cmd_branch_XXXXXX";
    assert(mkdtemp(repo_dir) != NULL);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git init \"%s\" >/dev/null 2>&1", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name tester", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email tester@example.com", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'hello\\n' > \"%s/README.md\"", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add README.md && git -C \"%s\" commit -m init >/dev/null 2>&1", repo_dir, repo_dir);
    assert(system(cmd) == 0);

    GooseConfig cfg = {0};
    cfg.working_dir = repo_dir;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "branch", "show", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "master") != NULL || strstr(result, "main") != NULL);
    free(result);

    result = command_registry_execute(&reg, "branch", "create feature/test", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "feature/test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "branch", "list", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "feature/test") != NULL);
    free(result);

    result = command_registry_execute(&reg, "branch", "switch master", &cfg, sess);
    if (strstr(result, "pathspec 'master' did not match") != NULL) {
        free(result);
        result = command_registry_execute(&reg, "branch", "switch main", &cfg, sess);
    }
    assert(result != NULL);
    assert(strstr(result, "Switched to branch") != NULL || strstr(result, "Already on") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", repo_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_branch_command_show_list_create_and_switch\n");
}

void test_branch_command_usage(void) {
    tests_run++;

    GooseConfig cfg = {0};
    cfg.working_dir = "/tmp";
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "branch", "bogus", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Usage:") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    tests_passed++;
    printf("  PASS: test_branch_command_usage\n");
}

void test_commit_command_creates_commit_and_handles_no_changes(void) {
    tests_run++;

    char repo_dir[] = "/tmp/goosecode_cmd_commit_XXXXXX";
    assert(mkdtemp(repo_dir) != NULL);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git init \"%s\" >/dev/null 2>&1", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name tester", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email tester@example.com", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'hello\\n' > \"%s/README.md\"", repo_dir);
    assert(system(cmd) == 0);

    GooseConfig cfg = {0};
    cfg.working_dir = repo_dir;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "commit", "Initial commit", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Initial commit") != NULL || strstr(result, "files changed") != NULL);
    free(result);

    result = command_registry_execute(&reg, "commit", "Second commit", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "No changes to commit.\n") == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", repo_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_commit_command_creates_commit_and_handles_no_changes\n");
}

void test_commit_command_rejects_secret_like_files(void) {
    tests_run++;

    char repo_dir[] = "/tmp/goosecode_cmd_commit_secret_XXXXXX";
    assert(mkdtemp(repo_dir) != NULL);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git init \"%s\" >/dev/null 2>&1", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name tester", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email tester@example.com", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'SECRET=1\\n' > \"%s/.env\"", repo_dir);
    assert(system(cmd) == 0);

    GooseConfig cfg = {0};
    cfg.working_dir = repo_dir;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "commit", "Do not commit secrets", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Error: refusing to commit files that look like secrets. Review the status output manually first.\n") == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", repo_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_commit_command_rejects_secret_like_files\n");
}

void test_review_command_reports_status_and_diff_checks(void) {
    tests_run++;

    char repo_dir[] = "/tmp/goosecode_cmd_review_XXXXXX";
    assert(mkdtemp(repo_dir) != NULL);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git init \"%s\" >/dev/null 2>&1", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name tester", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email tester@example.com", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'hello\\n' > \"%s/README.md\"", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add README.md && git -C \"%s\" commit -m init >/dev/null 2>&1", repo_dir, repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'staged\\n' > \"%s/staged.txt\"", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add staged.txt", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'more\\n' >> \"%s/README.md\"", repo_dir);
    assert(system(cmd) == 0);

    GooseConfig cfg = {0};
    cfg.working_dir = repo_dir;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "review", "", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "Review summary:") != NULL);
    assert(strstr(result, "Findings:") != NULL);
    assert(strstr(result, "Unstaged changes are present") != NULL);
    assert(strstr(result, "Current branch:") != NULL);
    assert(strstr(result, "staged.txt") != NULL);
    assert(strstr(result, "README.md") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", repo_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_review_command_reports_status_and_diff_checks\n");
}

void test_review_command_clean_tree(void) {
    tests_run++;

    char repo_dir[] = "/tmp/goosecode_cmd_review_clean_XXXXXX";
    assert(mkdtemp(repo_dir) != NULL);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git init \"%s\" >/dev/null 2>&1", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name tester", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email tester@example.com", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "printf 'hello\\n' > \"%s/README.md\"", repo_dir);
    assert(system(cmd) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add README.md && git -C \"%s\" commit -m init >/dev/null 2>&1", repo_dir, repo_dir);
    assert(system(cmd) == 0);

    GooseConfig cfg = {0};
    cfg.working_dir = repo_dir;
    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "review", "", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "No obvious local review findings.") != NULL);
    assert(strstr(result, "Working tree clean.") != NULL);
    assert(strstr(result, "No staged changes.") != NULL);
    assert(strstr(result, "No unstaged changes.") != NULL);
    free(result);

    command_registry_free(&reg);
    session_free(sess);
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", repo_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_review_command_clean_tree\n");
}

void test_subagents_command_list_show_clean_and_prune(void) {
    tests_run++;

    char base_dir[] = "/tmp/goosecode_cmd_subagents_XXXXXX";
    assert(mkdtemp(base_dir) != NULL);
    char sub_dir[1024];
    char worktree_dir[1024];
    snprintf(sub_dir, sizeof(sub_dir), "%s/subagents", base_dir);
    snprintf(worktree_dir, sizeof(worktree_dir), "%s/worktrees", base_dir);
    assert(mkdir(sub_dir, 0755) == 0);
    assert(mkdir(worktree_dir, 0755) == 0);

    GooseConfig cfg = {0};
    cfg.subagent_dir = sub_dir;
    cfg.worktree_dir = worktree_dir;

    SubagentRecord *record = subagent_record_new("subagent_test");
    free(record->status);
    record->status = strdup("completed");
    record->description = strdup("Completed subagent");
    record->subagent_type = strdup("explore");
    record->workspace_mode = strdup("direct");
    assert(subagent_record_save(&cfg, record) == NULL);
    subagent_record_free(record);

    char orphan_dir[2048];
    snprintf(orphan_dir, sizeof(orphan_dir), "%s/orphan_worktree", worktree_dir);
    assert(mkdir(orphan_dir, 0755) == 0);

    Session *sess = session_new();
    CommandRegistry reg = command_registry_init();
    command_registry_register_all(&reg);

    char *result = command_registry_execute(&reg, "subagents", "list", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "subagent_test [completed|explore|direct] Completed subagent") != NULL);
    free(result);

    result = command_registry_execute(&reg, "subagents", "show subagent_test", &cfg, sess);
    assert(result != NULL);
    assert(strstr(result, "task_id: subagent_test") != NULL);
    assert(strstr(result, "status: completed") != NULL);
    free(result);

    result = command_registry_execute(&reg, "subagents", "clean", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Removed 1 stale subagent record(s).\n") == 0);
    free(result);

    result = command_registry_execute(&reg, "subagents", "prune", &cfg, sess);
    assert(result != NULL);
    assert(strcmp(result, "Removed 1 orphaned worktree(s).\n") == 0);
    free(result);

    command_registry_free(&reg);
    session_free(sess);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", base_dir);
    assert(system(cmd) == 0);

    tests_passed++;
    printf("  PASS: test_subagents_command_list_show_clean_and_prune\n");
}

int main(void) {
    printf("Running tests...\n\n");

    test_strbuf_basic();
    test_strbuf_fmt();
    test_strbuf_trim();
    test_json_build_message();
    test_json_tool_def();
    test_perm_read_only();
    test_perm_allow();
    test_perm_deny_list();
    test_config_perm_mode_str();
    test_config_perm_mode_from_str();
    test_sse_multi_tool_calls_by_index();
    test_ask_user_question_single_select();
    test_ask_user_question_multiple_and_custom();
    test_ask_user_question_reprompts_invalid_choice();
    test_tool_definitions_hide_blocked_tools();
    test_tool_definitions_respect_allow_and_deny_lists();
    test_tool_definitions_include_repl_and_powershell_schemas();
    test_tool_definitions_include_message_and_config_schemas();
    test_session_plan_persistence();
    test_prompt_includes_plan_mode();
    test_plan_mode_tools_toggle_session();
    test_plan_command_updates_session();
    test_todo_write_persists_and_formats();
    test_todo_write_rejects_multiple_in_progress();
    test_task_tools_create_get_list_update();
    test_task_tools_see_todo_write_entries();
    test_task_update_rejects_unknown_task();
    test_subagent_record_persistence();
    test_agent_tool_schema_includes_task_id();
    test_agent_tool_rejects_unknown_subagent_type();
    test_agent_tool_rejects_missing_resume_id();
    test_mcp_list_and_read_resources();
    test_mcp_missing_server_and_resource_errors();
    test_lsp_hover_definition_and_symbols();
    test_lsp_rejects_unknown_action();
    test_tasks_command_create_list_show_and_set();
    test_tasks_command_rejects_bad_status();
    test_config_command_show_and_inspect();
    test_config_command_rejects_unknown_setting();
    test_branch_command_show_list_create_and_switch();
    test_branch_command_usage();
    test_commit_command_creates_commit_and_handles_no_changes();
    test_commit_command_rejects_secret_like_files();
    test_review_command_reports_status_and_diff_checks();
    test_review_command_clean_tree();
    test_subagents_command_list_show_clean_and_prune();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
