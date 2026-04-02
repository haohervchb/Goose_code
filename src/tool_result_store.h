#ifndef TOOL_RESULT_STORE_H
#define TOOL_RESULT_STORE_H

#include "config.h"
#include "session.h"

char *tool_result_store_prepare(const GooseConfig *cfg, const Session *sess,
                                const char *tool_call_id, const char *content);

#endif
