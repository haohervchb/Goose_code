#ifndef TASK_STORE_H
#define TASK_STORE_H

#include "config.h"
#include "util/cJSON.h"

char *task_store_load(const GooseConfig *cfg, cJSON **tasks_out);
char *task_store_validate_and_normalize(cJSON *tasks, int allow_empty);
char *task_store_save(const GooseConfig *cfg, const cJSON *tasks);
cJSON *task_store_find(cJSON *tasks, const char *task_id);

#endif
