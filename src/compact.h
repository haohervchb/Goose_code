#ifndef COMPACT_H
#define COMPACT_H

#include "api.h"
#include "util/cJSON.h"

typedef enum {
    COMPACT_PARTIAL_FROM,
    COMPACT_PARTIAL_UP_TO,
} CompactPartialDirection;

char *compact_get_prompt(void);
char *compact_get_partial_prompt(CompactPartialDirection direction);
char *compact_format_summary(const char *summary);
char *compact_build_user_summary_message(const char *summary, int recent_messages_preserved);
char *compact_generate_summary(const ApiConfig *cfg, const cJSON *messages);
char *compact_generate_partial_summary(const ApiConfig *cfg, const cJSON *messages,
                                      CompactPartialDirection direction);
char *compact_summarize(const char *messages_json, int keep_recent);

#endif
