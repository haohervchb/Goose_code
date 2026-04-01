#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/util/strbuf.h"
#include "../src/util/json_util.h"
#include "../src/util/sse.h"
#include "../src/permissions.h"
#include "../src/config.h"
#include "../src/tools/tools.h"

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

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
