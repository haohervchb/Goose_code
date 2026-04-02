#ifndef CONFIG_H
#define CONFIG_H

#include "util/cJSON.h"

typedef enum {
    PERM_READ_ONLY,
    PERM_WORKSPACE_WRITE,
    PERM_DANGER_FULL_ACCESS,
    PERM_PROMPT,
    PERM_ALLOW
} PermissionMode;

typedef struct {
    char *base_url;
    char *api_key;
    char *model;
    PermissionMode permission_mode;
    int max_tokens;
    double temperature;
    int max_turns;
    int context_window;
    char *working_dir;
    char *session_dir;
    char *subagent_dir;
    char *todo_store;
    char *skill_dir;
    cJSON *mcp_servers;
    cJSON *allowed_tools;
    cJSON *denied_tools;
    int verbose;
} GooseConfig;

GooseConfig config_load(void);
void config_free(GooseConfig *cfg);
const char *config_perm_mode_str(PermissionMode mode);
PermissionMode config_perm_mode_from_str(const char *s);
char *config_get_home_dir(void);

#endif
