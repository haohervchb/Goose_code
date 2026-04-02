#ifndef PROMPT_H
#define PROMPT_H

#include "config.h"
#include "session.h"
#include "util/cJSON.h"

char *prompt_build_static_system_prefix(const GooseConfig *cfg, const Session *sess, const char *working_dir);
char *prompt_build_dynamic_system_suffix(const GooseConfig *cfg, const Session *sess, const char *working_dir);
char *prompt_build_default_system(const GooseConfig *cfg, const Session *sess, const char *working_dir);
char *prompt_build_effective_system(const GooseConfig *cfg, const Session *sess,
                                    const char *working_dir, const char *agent_system_prompt);
char *prompt_build_system(const GooseConfig *cfg, const Session *sess, const char *working_dir);
cJSON *prompt_build_user_message(const char *text);
cJSON *prompt_build_messages_with_tools(const cJSON *system_msg, const cJSON *history,
                                          const cJSON *user_msg);

#endif
