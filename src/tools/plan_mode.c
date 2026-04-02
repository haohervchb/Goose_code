#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_enter_plan_mode(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    Session *sess = tool_context_get_session();
    if (!sess) return strdup("Error: no active session for plan mode");

    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *plan = json_get_string(json, "plan");
    if (!plan) plan = json_get_string(json, "description");

    session_set_plan_mode(sess, 1);
    if (plan && plan[0]) {
        session_set_plan(sess, plan);
    }
    cJSON_Delete(json);

    StrBuf out = strbuf_new();
    strbuf_append(&out, "Plan mode enabled.");
    if (sess->plan_content && sess->plan_content[0]) {
        strbuf_append_fmt(&out, "\nCurrent plan:\n%s", sess->plan_content);
    } else {
        strbuf_append(&out, "\nNo plan has been written yet.");
    }
    return strbuf_detach(&out);
}

char *tool_execute_exit_plan_mode(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    Session *sess = tool_context_get_session();
    if (!sess) return strdup("Error: no active session for plan mode");

    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");
    cJSON_Delete(json);

    session_set_plan_mode(sess, 0);
    return strdup("Plan mode disabled.");
}
