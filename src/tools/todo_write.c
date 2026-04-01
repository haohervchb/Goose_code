#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_todo_write(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    cJSON *todos = cJSON_GetObjectItem(json, "todos");
    cJSON_Delete(json);

    if (!todos || !cJSON_IsArray(todos)) return strdup("Error: 'todos' array required");

    int count = cJSON_GetArraySize(todos);
    if (count == 0) return strdup("Error: todos array must not be empty");
    if (count > 50) return strdup("Error: too many todos (max 50)");

    int in_progress_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, todos) {
        const char *status = json_get_string(item, "status");
        const char *content = json_get_string(item, "content");
        if (!content || strlen(content) == 0) {
            return strdup("Error: each todo must have non-empty 'content'");
        }
        if (status && strcmp(status, "in_progress") == 0) {
            in_progress_count++;
        }
    }
    if (in_progress_count > 1) {
        return strdup("Error: only one todo can be 'in_progress' at a time");
    }

    json_write_file(cfg->todo_store, todos);

    StrBuf out = strbuf_new();
    strbuf_append(&out, "Todos updated:\n");
    int i = 0;
    cJSON_ArrayForEach(item, todos) {
        const char *content = json_get_string(item, "content");
        const char *status = json_get_string(item, "status");
        strbuf_append_fmt(&out, "  %d. [%s] %s\n", ++i, status ? status : "pending", content);
    }
    if (in_progress_count == 1) {
        strbuf_append(&out, "\nRemember to verify your work when you complete the in-progress task.");
    }
    return strbuf_detach(&out);
}
