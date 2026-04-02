#include "tools/tools.h"
#include "tools/task_store.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *task_result_single(const cJSON *task, const char *key) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, key, cJSON_Duplicate((cJSON *)task, 1));
    char *out = json_to_string(result);
    cJSON_Delete(result);
    return out ? out : strdup("Error: failed to encode task result");
}

char *tool_execute_task_create(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *content = json_get_string(json, "content");
    const char *status = json_get_string(json, "status");
    const char *priority = json_get_string(json, "priority");
    if (!content || content[0] == '\0') {
        cJSON_Delete(json);
        return strdup("Error: 'content' argument required");
    }

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    cJSON *task = cJSON_CreateObject();
    cJSON_AddStringToObject(task, "content", content);
    if (status) cJSON_AddStringToObject(task, "status", status);
    if (priority) cJSON_AddStringToObject(task, "priority", priority);
    cJSON_AddItemToArray(tasks, task);

    err = task_store_validate_and_normalize(tasks, 0);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    err = task_store_save(cfg, tasks);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    char *out = task_result_single(task, "task");
    cJSON_Delete(tasks);
    cJSON_Delete(json);
    return out;
}

char *tool_execute_task_get(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *task_id = json_get_string(json, "task_id");
    if (!task_id || task_id[0] == '\0') {
        cJSON_Delete(json);
        return strdup("Error: 'task_id' argument required");
    }

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    cJSON *task = task_store_find(tasks, task_id);
    if (!task) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return strdup("Error: task not found");
    }

    char *out = task_result_single(task, "task");
    cJSON_Delete(tasks);
    cJSON_Delete(json);
    return out;
}

char *tool_execute_task_list(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");
    const char *status_filter = json_get_string(json, "status");
    const char *priority_filter = json_get_string(json, "priority");

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *filtered = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "tasks", filtered);

    cJSON *item;
    cJSON_ArrayForEach(item, tasks) {
        const char *status = json_get_string(item, "status");
        const char *priority = json_get_string(item, "priority");
        if (status_filter && (!status || strcmp(status, status_filter) != 0)) continue;
        if (priority_filter && (!priority || strcmp(priority, priority_filter) != 0)) continue;
        cJSON_AddItemToArray(filtered, cJSON_Duplicate(item, 1));
    }

    char *out = json_to_string(result);
    cJSON_Delete(result);
    cJSON_Delete(tasks);
    cJSON_Delete(json);
    return out ? out : strdup("Error: failed to encode task list");
}

char *tool_execute_task_update(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *task_id = json_get_string(json, "task_id");
    const char *content = json_get_string(json, "content");
    const char *status = json_get_string(json, "status");
    const char *priority = json_get_string(json, "priority");
    if (!task_id || task_id[0] == '\0') {
        cJSON_Delete(json);
        return strdup("Error: 'task_id' argument required");
    }
    if (!content && !status && !priority) {
        cJSON_Delete(json);
        return strdup("Error: provide at least one of 'content', 'status', or 'priority'");
    }

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) {
        cJSON_Delete(json);
        return err;
    }

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    cJSON *task = task_store_find(tasks, task_id);
    if (!task) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return strdup("Error: task not found");
    }

    if (content) cJSON_ReplaceItemInObject(task, "content", cJSON_CreateString(content));
    if (status) cJSON_ReplaceItemInObject(task, "status", cJSON_CreateString(status));
    if (priority) cJSON_ReplaceItemInObject(task, "priority", cJSON_CreateString(priority));

    err = task_store_validate_and_normalize(tasks, 0);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    err = task_store_save(cfg, tasks);
    if (err) {
        cJSON_Delete(tasks);
        cJSON_Delete(json);
        return err;
    }

    char *out = task_result_single(task, "task");
    cJSON_Delete(tasks);
    cJSON_Delete(json);
    return out;
}
