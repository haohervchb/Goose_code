#include "tools/task_store.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int task_status_valid(const char *status) {
    return strcmp(status, "pending") == 0 || strcmp(status, "in_progress") == 0 ||
           strcmp(status, "completed") == 0 || strcmp(status, "cancelled") == 0;
}

static int task_priority_valid(const char *priority) {
    return strcmp(priority, "high") == 0 || strcmp(priority, "medium") == 0 ||
           strcmp(priority, "low") == 0;
}

static void task_set_string(cJSON *item, const char *key, const char *value) {
    cJSON *existing = cJSON_GetObjectItem(item, key);
    cJSON *replacement = cJSON_CreateString(value);
    if (existing) cJSON_ReplaceItemInObject(item, key, replacement);
    else cJSON_AddItemToObject(item, key, replacement);
}

static int task_parse_numeric_suffix(const char *task_id) {
    if (!task_id || strncmp(task_id, "task_", 5) != 0) return -1;
    const char *suffix = task_id + 5;
    if (!*suffix) return -1;
    for (const char *p = suffix; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
    }
    return atoi(suffix);
}

char *task_store_load(const GooseConfig *cfg, cJSON **tasks_out) {
    *tasks_out = NULL;
    char *data = json_read_file(cfg->todo_store);
    if (!data) {
        *tasks_out = cJSON_CreateArray();
        return NULL;
    }

    int only_whitespace = 1;
    for (char *p = data; *p; p++) {
        if (*p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
            only_whitespace = 0;
            break;
        }
    }
    if (only_whitespace) {
        free(data);
        *tasks_out = cJSON_CreateArray();
        return NULL;
    }

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return strdup("Error: failed to parse task store JSON");
    if (!cJSON_IsArray(json)) {
        cJSON_Delete(json);
        return strdup("Error: task store must contain a JSON array");
    }

    *tasks_out = json;
    return NULL;
}

char *task_store_validate_and_normalize(cJSON *tasks, int allow_empty) {
    if (!tasks || !cJSON_IsArray(tasks)) return strdup("Error: task store must be an array");

    int count = cJSON_GetArraySize(tasks);
    if (!allow_empty && count == 0) return strdup("Error: task list must not be empty");
    if (count > 50) return strdup("Error: too many tasks (max 50)");

    int next_id = 1;
    int in_progress_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, tasks) {
        if (!cJSON_IsObject(item)) return strdup("Error: each task must be an object");
        const char *task_id = json_get_string(item, "id");
        int numeric = task_parse_numeric_suffix(task_id);
        if (numeric >= next_id) next_id = numeric + 1;
    }

    cJSON_ArrayForEach(item, tasks) {
        const char *content = json_get_string(item, "content");
        const char *status = json_get_string(item, "status");
        const char *priority = json_get_string(item, "priority");
        const char *task_id = json_get_string(item, "id");

        if (!content || content[0] == '\0') return strdup("Error: each task must have non-empty 'content'");

        if (!status || status[0] == '\0') {
            task_set_string(item, "status", "pending");
            status = json_get_string(item, "status");
        }
        if (!task_status_valid(status)) return strdup("Error: task status must be pending, in_progress, completed, or cancelled");

        if (!priority || priority[0] == '\0') {
            task_set_string(item, "priority", "medium");
            priority = json_get_string(item, "priority");
        }
        if (!task_priority_valid(priority)) return strdup("Error: task priority must be high, medium, or low");

        if (!task_id || task_id[0] == '\0') {
            char generated[32];
            snprintf(generated, sizeof(generated), "task_%d", next_id++);
            task_set_string(item, "id", generated);
        }

        if (strcmp(status, "in_progress") == 0) in_progress_count++;
    }

    if (in_progress_count > 1) return strdup("Error: only one task can be 'in_progress' at a time");
    return NULL;
}

char *task_store_save(const GooseConfig *cfg, const cJSON *tasks) {
    if (json_write_file(cfg->todo_store, tasks) != 0) {
        return strdup("Error: failed to save task store");
    }
    return NULL;
}

cJSON *task_store_find(cJSON *tasks, const char *task_id) {
    if (!tasks || !task_id) return NULL;
    cJSON *item;
    cJSON_ArrayForEach(item, tasks) {
        const char *id = json_get_string(item, "id");
        if (id && strcmp(id, task_id) == 0) return item;
    }
    return NULL;
}
