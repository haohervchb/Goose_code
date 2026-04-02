#ifndef CONFIG_H
#define CONFIG_H

#include "util/cJSON.h"
#include <stddef.h>

typedef enum {
    PERM_READ_ONLY,
    PERM_WORKSPACE_WRITE,
    PERM_DANGER_FULL_ACCESS,
    PERM_PROMPT,
    PERM_ALLOW
} PermissionMode;

typedef struct {
    const char *name;
    const char *display_name;
    const char *default_base_url;
    const char *default_model;
    int requires_api_key;
    int supports_model_list;
} ProviderProfile;

typedef struct {
    char *provider;
    char *base_url;
    char *api_key;
    char *model;
    char *system_prompt;
    char *append_system_prompt;
    char *override_system_prompt;
    char *output_style;
    char *response_language;
    PermissionMode permission_mode;
    int max_tokens;
    double temperature;
    int max_turns;
    int context_window;
    char *working_dir;
    char *session_dir;
    char *session_memory_dir;
    char *tool_result_dir;
    char *subagent_dir;
    char *worktree_dir;
    char *todo_store;
    char *skill_dir;
    cJSON *mcp_servers;
    cJSON *allowed_tools;
    cJSON *denied_tools;
    int verbose;
} GooseConfig;

GooseConfig config_load(void);
void config_free(GooseConfig *cfg);
char *config_user_settings_path(void);
int config_save_user_settings(const GooseConfig *cfg);
int config_load_user_provider_settings(const char *provider, char **base_url_out,
                                       char **model_out, char **api_key_out);
const char *config_perm_mode_str(PermissionMode mode);
PermissionMode config_perm_mode_from_str(const char *s);
char *config_get_home_dir(void);
const ProviderProfile *provider_profile_find(const char *name);
const ProviderProfile *provider_profile_detect(const GooseConfig *cfg);
size_t provider_profile_count(void);
const ProviderProfile *provider_profile_at(size_t index);
int provider_apply_preset(GooseConfig *cfg, const char *name, int update_model);
int provider_requires_api_key(const GooseConfig *cfg);
char *provider_list_models(const GooseConfig *cfg);
char *provider_test_connection(const GooseConfig *cfg);

#endif
