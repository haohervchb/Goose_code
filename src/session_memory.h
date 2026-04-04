#ifndef SESSION_MEMORY_H
#define SESSION_MEMORY_H

#include "api.h"
#include "config.h"
#include "session.h"

#define SESSION_MEMORY_MAX_LINES 200
#define SESSION_MEMORY_MAX_BYTES 25000

typedef struct {
    char *truncated_content;
    int was_truncated;
} SessionMemoryTruncateResult;

char *session_memory_default_template(void);
char *session_memory_default_update_prompt(void);
char *session_memory_path(const GooseConfig *cfg, const Session *sess);
int session_memory_ensure(const GooseConfig *cfg, const Session *sess);
char *session_memory_load(const GooseConfig *cfg, const Session *sess);
char *session_memory_build_update_prompt(const char *current_notes, const char *notes_path);
int session_memory_update(const GooseConfig *cfg, const Session *sess, const ApiConfig *api_cfg);
SessionMemoryTruncateResult session_memory_truncate_for_compact(const char *content);
void session_memory_truncate_result_free(SessionMemoryTruncateResult *result);
char *session_memory_truncate_for_display(const char *content);

#endif
