#ifndef SESSION_MEMORY_H
#define SESSION_MEMORY_H

#include "api.h"
#include "config.h"
#include "session.h"

char *session_memory_default_template(void);
char *session_memory_default_update_prompt(void);
char *session_memory_path(const GooseConfig *cfg, const Session *sess);
int session_memory_ensure(const GooseConfig *cfg, const Session *sess);
char *session_memory_load(const GooseConfig *cfg, const Session *sess);
char *session_memory_build_update_prompt(const char *current_notes, const char *notes_path);
int session_memory_update(const GooseConfig *cfg, const Session *sess, const ApiConfig *api_cfg);

#endif
