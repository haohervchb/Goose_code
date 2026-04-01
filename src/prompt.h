#ifndef PROMPT_H
#define PROMPT_H

#include "config.h"
#include "util/cJSON.h"

char *prompt_build_system(const GooseConfig *cfg, const char *working_dir);
cJSON *prompt_build_user_message(const char *text);
cJSON *prompt_build_messages_with_tools(const cJSON *system_msg, const cJSON *history,
                                         const cJSON *user_msg);

#endif
