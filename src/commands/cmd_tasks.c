#include "commands/commands.h"
#include "tools/task_store.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int task_status_matches(const char *status) {
    return status && (
        strcmp(status, "pending") == 0 ||
        strcmp(status, "in_progress") == 0 ||
        strcmp(status, "completed") == 0 ||
        strcmp(status, "cancelled") == 0
    );
}

static void append_task_line(StrBuf *out, cJSON *task) {
    const char *id = json_get_string(task, "id");
    const char *content = json_get_string(task, "content");
    const char *status = json_get_string(task, "status");
    const char *priority = json_get_string(task, "priority");
    strbuf_append_fmt(out, "%s [%s|%s] %s\n",
                      id ? id : "task_unknown",
                      status ? status : "pending",
                      priority ? priority : "medium",
                      content ? content : "");
}

static char *tasks_render_list(const GooseConfig *cfg, const char *status_filter) {
    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) return err;

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    StrBuf out = strbuf_new();
    int count = 0;
    cJSON *task;
    cJSON_ArrayForEach(task, tasks) {
        const char *status = json_get_string(task, "status");
        if (status_filter && (!status || strcmp(status, status_filter) != 0)) continue;
        append_task_line(&out, task);
        count++;
    }

    if (count == 0) {
        if (status_filter) strbuf_append_fmt(&out, "No tasks with status %s.\n", status_filter);
        else strbuf_append(&out, "No tasks.\n");
    }

    char *result = strbuf_detach(&out);
    cJSON_Delete(tasks);
    return result;
}

static char *tasks_show_one(const GooseConfig *cfg, const char *task_id) {
    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) return err;

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    cJSON *task = task_store_find(tasks, task_id);
    if (!task) {
        cJSON_Delete(tasks);
        return strdup("Task not found.\n");
    }

    StrBuf out = strbuf_new();
    append_task_line(&out, task);
    char *result = strbuf_detach(&out);
    cJSON_Delete(tasks);
    return result;
}

static char *tasks_create_one(const GooseConfig *cfg, const char *content) {
    if (!content || !content[0]) return strdup("Usage: /tasks create <content>\n");

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) return err;

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    cJSON *task = cJSON_CreateObject();
    cJSON_AddStringToObject(task, "content", content);
    cJSON_AddStringToObject(task, "status", "pending");
    cJSON_AddStringToObject(task, "priority", "medium");
    cJSON_AddItemToArray(tasks, task);

    err = task_store_validate_and_normalize(tasks, 0);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    err = task_store_save(cfg, tasks);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    StrBuf out = strbuf_from("Created task:\n");
    append_task_line(&out, task);
    char *result = strbuf_detach(&out);
    cJSON_Delete(tasks);
    return result;
}

static char *tasks_set_status(const GooseConfig *cfg, const char *task_id, const char *status) {
    if (!task_id || !task_id[0] || !status || !status[0]) {
        return strdup("Usage: /tasks set <task_id> <pending|in_progress|completed|cancelled>\n");
    }
    if (!task_status_matches(status)) {
        return strdup("Error: status must be pending, in_progress, completed, or cancelled\n");
    }

    cJSON *tasks = NULL;
    char *err = task_store_load(cfg, &tasks);
    if (err) return err;

    err = task_store_validate_and_normalize(tasks, 1);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    cJSON *task = task_store_find(tasks, task_id);
    if (!task) {
        cJSON_Delete(tasks);
        return strdup("Task not found.\n");
    }

    cJSON_ReplaceItemInObject(task, "status", cJSON_CreateString(status));
    err = task_store_validate_and_normalize(tasks, 0);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    err = task_store_save(cfg, tasks);
    if (err) {
        cJSON_Delete(tasks);
        return err;
    }

    StrBuf out = strbuf_from("Updated task:\n");
    append_task_line(&out, task);
    char *result = strbuf_detach(&out);
    cJSON_Delete(tasks);
    return result;
}

static char *cmd_tasks_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;

    if (!args || !args[0]) {
        return tasks_render_list(cfg, NULL);
    }

    if (strcmp(args, "list") == 0) {
        return tasks_render_list(cfg, NULL);
    }
    if (strcmp(args, "pending") == 0 || strcmp(args, "in_progress") == 0 ||
        strcmp(args, "completed") == 0 || strcmp(args, "cancelled") == 0) {
        return tasks_render_list(cfg, args);
    }

    if (strncmp(args, "show ", 5) == 0) {
        return tasks_show_one(cfg, args + 5);
    }
    if (strncmp(args, "create ", 7) == 0) {
        return tasks_create_one(cfg, args + 7);
    }
    if (strncmp(args, "set ", 4) == 0) {
        const char *rest = args + 4;
        const char *space = strchr(rest, ' ');
        if (!space) return strdup("Usage: /tasks set <task_id> <pending|in_progress|completed|cancelled>\n");
        char *task_id = strndup(rest, (size_t)(space - rest));
        const char *status = space + 1;
        char *result = tasks_set_status(cfg, task_id, status);
        free(task_id);
        return result;
    }

    return strdup(
        "Usage:\n"
        "/tasks\n"
        "/tasks list\n"
        "/tasks <status>\n"
        "/tasks show <task_id>\n"
        "/tasks create <content>\n"
        "/tasks set <task_id> <pending|in_progress|completed|cancelled>\n");
}

void cmd_tasks_register(CommandRegistry *reg) {
    Command cmd = {strdup("tasks"), strdup("Inspect and update tracked tasks"), 1, cmd_tasks_exec};
    command_registry_register(reg, cmd);
}
