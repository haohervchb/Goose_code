#include "tools/tools.h"
#include "tools/task_store.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_todo_write(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    cJSON *todos = cJSON_GetObjectItem(json, "todos");
    if (!todos || !cJSON_IsArray(todos)) {
        cJSON_Delete(json);
        return strdup("Error: 'todos' array required");
    }

    char *err = task_store_validate_and_normalize(todos, 0);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    err = task_store_save(cfg, todos);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    StrBuf out = strbuf_new();
    strbuf_append(&out, "Todos updated:\n");
    int i = 0;
    int in_progress_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, todos) {
        const char *task_id = json_get_string(item, "id");
        const char *content = json_get_string(item, "content");
        const char *status = json_get_string(item, "status");
        const char *priority = json_get_string(item, "priority");
        if (status && strcmp(status, "in_progress") == 0) in_progress_count++;
        strbuf_append_fmt(&out, "  %d. [%s|%s] %s (%s)\n", ++i,
                          status ? status : "pending",
                          priority ? priority : "medium",
                          content,
                          task_id ? task_id : "no-id");
    }
    if (in_progress_count == 1) {
        strbuf_append(&out, "\nRemember to verify your work when you complete the in-progress task.");
    }
    char *result = strbuf_detach(&out);
    cJSON_Delete(json);
    return result;
}
