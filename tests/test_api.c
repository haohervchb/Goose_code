#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/util/strbuf.h"
#include "../src/util/json_util.h"
#include "../src/util/sse.h"
#include "../src/permissions.h"
#include "../src/config.h"
#include "../src/session.h"
#include "../src/prompt.h"
#include "../src/tools/tools.h"
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

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
