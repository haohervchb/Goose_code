#ifndef SUBAGENT_STORE_H
#define SUBAGENT_STORE_H

#include "config.h"
#include "util/cJSON.h"

typedef struct {
    char *task_id;
    char *status;
    char *description;
    char *subagent_type;
    char *model;
    char *working_dir;
    char *workspace_mode;
    char *result;
    char *error;
    int fork_mode;
    cJSON *messages;
} SubagentRecord;

SubagentRecord *subagent_record_new(const char *task_id);
SubagentRecord *subagent_record_load(const GooseConfig *cfg, const char *task_id);
char *subagent_record_save(const GooseConfig *cfg, const SubagentRecord *record);
void subagent_record_free(SubagentRecord *record);

#endif
