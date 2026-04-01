#include "config.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

char *config_get_home_dir(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    return home ? strdup(home) : strdup("/tmp");
}

static char *config_path(const char *home, const char *rel) {
    size_t len = strlen(home) + strlen(rel) + 2;
    char *p = malloc(len);
    snprintf(p, len, "%s/%s", home, rel);
    return p;
}

static void ensure_dir(const char *path) {
    mkdir(path, 0755);
}

static cJSON *load_json_file(const char *path) {
    char *data = json_read_file(path);
    if (!data) return NULL;
    cJSON *json = cJSON_Parse(data);
    free(data);
    return json;
}

static void merge_config(cJSON *target, cJSON *source) {
    if (!target || !source) return;
    cJSON *item;
    cJSON_ArrayForEach(item, source) {
        cJSON *existing = cJSON_GetObjectItem(target, item->string);
        if (!existing) {
            cJSON_AddItemToObject(target, item->string, cJSON_Duplicate(item, 1));
        }
    }
}

GooseConfig config_load(void) {
    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_PROMPT;
    cfg.max_tokens = 8192;
    cfg.temperature = 0.7;
    cfg.max_turns = 64;
    cfg.context_window = 128000;
    cfg.verbose = 0;

    char *home = config_get_home_dir();
    cfg.working_dir = getcwd(NULL, 0);
    if (!cfg.working_dir) cfg.working_dir = strdup(".");

    cfg.session_dir = config_path(home, ".goosecode/sessions");
    ensure_dir(config_path(home, ".goosecode"));
    ensure_dir(cfg.session_dir);

    cfg.todo_store = config_path(home, ".goosecode/todos.json");
    cfg.skill_dir = config_path(home, ".goosecode/skills");
    ensure_dir(cfg.skill_dir);

    const char *env_base = getenv("OPENAI_BASE_URL");
    if (env_base) cfg.base_url = strdup(env_base);
    else cfg.base_url = strdup("https://api.openai.com/v1");

    const char *env_key = getenv("OPENAI_API_KEY");
    if (env_key) cfg.api_key = strdup(env_key);

    const char *env_model = getenv("OPENAI_MODEL");
    if (env_model) cfg.model = strdup(env_model);
    else cfg.model = strdup("gpt-4o");

    const char *env_perms = getenv("GOOSECODE_PERMS");
    if (env_perms) cfg.permission_mode = config_perm_mode_from_str(env_perms);

    const char *env_turns = getenv("GOOSECODE_MAX_TURNS");
    if (env_turns) cfg.max_turns = atoi(env_turns);

    cJSON *mcp = cJSON_CreateArray();
    cfg.mcp_servers = mcp;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    char *proj_settings = config_path(cfg.working_dir, ".goosecode/settings.json");
    cJSON *proj = load_json_file(proj_settings);
    if (proj) {
        const char *v;
        v = json_get_string(proj, "model"); if (v) { free(cfg.model); cfg.model = strdup(v); }
        v = json_get_string(proj, "base_url"); if (v) { free(cfg.base_url); cfg.base_url = strdup(v); }
        v = json_get_string(proj, "permission_mode"); if (v) cfg.permission_mode = config_perm_mode_from_str(v);
        cfg.max_tokens = json_get_int(proj, "max_tokens", cfg.max_tokens);
        cfg.max_turns = json_get_int(proj, "max_turns", cfg.max_turns);
        cJSON *mt = json_get_array(proj, "allowed_tools");
        if (mt) { cJSON_Delete(cfg.allowed_tools); cfg.allowed_tools = cJSON_Duplicate(mt, 1); }
        cJSON *dt = json_get_array(proj, "denied_tools");
        if (dt) { cJSON_Delete(cfg.denied_tools); cfg.denied_tools = cJSON_Duplicate(dt, 1); }
        cJSON_Delete(proj);
    }
    free(proj_settings);

    char *user_settings = config_path(home, ".goosecode/settings.json");
    cJSON *user = load_json_file(user_settings);
    if (user) {
        const char *v;
        v = json_get_string(user, "model"); if (v && !env_model) { free(cfg.model); cfg.model = strdup(v); }
        v = json_get_string(user, "base_url"); if (v && !env_base) { free(cfg.base_url); cfg.base_url = strdup(v); }
        v = json_get_string(user, "api_key"); if (v) { if (cfg.api_key) free(cfg.api_key); cfg.api_key = strdup(v); }
        v = json_get_string(user, "permission_mode"); if (v && !env_perms) cfg.permission_mode = config_perm_mode_from_str(v);
        cfg.max_tokens = json_get_int(user, "max_tokens", cfg.max_tokens);
        cfg.max_turns = json_get_int(user, "max_turns", cfg.max_turns);
        cJSON_Delete(user);
    }
    free(user_settings);
    free(home);
    return cfg;
}

void config_free(GooseConfig *cfg) {
    free(cfg->base_url);
    free(cfg->api_key);
    free(cfg->model);
    free(cfg->working_dir);
    free(cfg->session_dir);
    free(cfg->todo_store);
    free(cfg->skill_dir);
    if (cfg->mcp_servers) cJSON_Delete(cfg->mcp_servers);
    if (cfg->allowed_tools) cJSON_Delete(cfg->allowed_tools);
    if (cfg->denied_tools) cJSON_Delete(cfg->denied_tools);
}

const char *config_perm_mode_str(PermissionMode mode) {
    switch (mode) {
        case PERM_READ_ONLY: return "read-only";
        case PERM_WORKSPACE_WRITE: return "workspace-write";
        case PERM_DANGER_FULL_ACCESS: return "danger-full-access";
        case PERM_PROMPT: return "prompt";
        case PERM_ALLOW: return "allow";
        default: return "unknown";
    }
}

PermissionMode config_perm_mode_from_str(const char *s) {
    if (!s) return PERM_PROMPT;
    if (strcmp(s, "read-only") == 0) return PERM_READ_ONLY;
    if (strcmp(s, "workspace-write") == 0) return PERM_WORKSPACE_WRITE;
    if (strcmp(s, "danger-full-access") == 0) return PERM_DANGER_FULL_ACCESS;
    if (strcmp(s, "allow") == 0) return PERM_ALLOW;
    return PERM_PROMPT;
}
