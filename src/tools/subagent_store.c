#include "tools/subagent_store.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *subagent_record_path(const GooseConfig *cfg, const char *task_id) {
    size_t len = strlen(cfg->subagent_dir) + strlen(task_id) + 16;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s.json", cfg->subagent_dir, task_id);
    return path;
}

SubagentRecord *subagent_record_new(const char *task_id) {
    SubagentRecord *record = calloc(1, sizeof(*record));
    record->task_id = strdup(task_id);
    record->status = strdup("pending");
    record->messages = cJSON_CreateArray();
    return record;
}

SubagentRecord *subagent_record_load(const GooseConfig *cfg, const char *task_id) {
    char *path = subagent_record_path(cfg, task_id);
    char *data = json_read_file(path);
    free(path);
    if (!data) return NULL;

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return NULL;

    SubagentRecord *record = calloc(1, sizeof(*record));
    const char *s = NULL;

    s = json_get_string(json, "task_id");
    record->task_id = strdup(s ? s : task_id);
    s = json_get_string(json, "status");
    record->status = strdup(s ? s : "pending");
    s = json_get_string(json, "description");
    if (s) record->description = strdup(s);
    s = json_get_string(json, "subagent_type");
    if (s) record->subagent_type = strdup(s);
    s = json_get_string(json, "model");
    if (s) record->model = strdup(s);
    s = json_get_string(json, "working_dir");
    if (s) record->working_dir = strdup(s);
    s = json_get_string(json, "workspace_mode");
    if (s) record->workspace_mode = strdup(s);
    s = json_get_string(json, "result");
    if (s) record->result = strdup(s);
    s = json_get_string(json, "error");
    if (s) record->error = strdup(s);

    record->fork_mode = json_get_int(json, "fork_mode", 0);

    cJSON *messages = json_get_array(json, "messages");
    record->messages = messages ? cJSON_Duplicate(messages, 1) : cJSON_CreateArray();
    cJSON_Delete(json);
    return record;
}

char *subagent_record_save(const GooseConfig *cfg, const SubagentRecord *record) {
    char *path = subagent_record_path(cfg, record->task_id);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "task_id", record->task_id);
    cJSON_AddStringToObject(json, "status", record->status ? record->status : "pending");
    if (record->description) cJSON_AddStringToObject(json, "description", record->description);
    if (record->subagent_type) cJSON_AddStringToObject(json, "subagent_type", record->subagent_type);
    if (record->model) cJSON_AddStringToObject(json, "model", record->model);
    if (record->working_dir) cJSON_AddStringToObject(json, "working_dir", record->working_dir);
    if (record->workspace_mode) cJSON_AddStringToObject(json, "workspace_mode", record->workspace_mode);
    if (record->result) cJSON_AddStringToObject(json, "result", record->result);
    if (record->error) cJSON_AddStringToObject(json, "error", record->error);
    cJSON_AddBoolToObject(json, "fork_mode", record->fork_mode);
    cJSON_AddItemToObject(json, "messages", record->messages ? cJSON_Duplicate(record->messages, 1) : cJSON_CreateArray());

    int rc = json_write_file(path, json);
    cJSON_Delete(json);
    free(path);
    if (rc != 0) return strdup("Error: failed to save subagent record");
    return NULL;
}

void subagent_record_free(SubagentRecord *record) {
    if (!record) return;
    free(record->task_id);
    free(record->status);
    free(record->description);
    free(record->subagent_type);
    free(record->model);
    free(record->working_dir);
    free(record->workspace_mode);
    free(record->result);
    free(record->error);
    if (record->messages) cJSON_Delete(record->messages);
    free(record);
}
