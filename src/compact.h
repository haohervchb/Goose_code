#ifndef COMPACT_H
#define COMPACT_H

#include "api.h"
#include "util/cJSON.h"

char *compact_get_prompt(void);
char *compact_format_summary(const char *summary);
char *compact_generate_summary(const ApiConfig *cfg, const cJSON *messages);
char *compact_summarize(const char *messages_json, int keep_recent);

#endif
