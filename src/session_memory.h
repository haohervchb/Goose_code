#ifndef SESSION_MEMORY_H
#define SESSION_MEMORY_H

#include "config.h"
#include "session.h"

char *session_memory_default_template(void);
char *session_memory_path(const GooseConfig *cfg, const Session *sess);
int session_memory_ensure(const GooseConfig *cfg, const Session *sess);
char *session_memory_load(const GooseConfig *cfg, const Session *sess);

#endif
