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

char *config_user_settings_path(void) {
    char *home = config_get_home_dir();
    char *path = config_path(home, ".goosecode/settings.json");
    free(home);
    return path;
}

static void merge_mcp_servers(cJSON **target, cJSON *source) {
    if (!source || !cJSON_IsArray(source)) return;
    if (!*target) *target = cJSON_CreateArray();

    cJSON *item;
    cJSON_ArrayForEach(item, source) {
        cJSON_AddItemToArray(*target, cJSON_Duplicate(item, 1));
    }
}

static void json_set_string(cJSON *obj, const char *key, const char *value) {
    cJSON *item = cJSON_CreateString(value ? value : "");
    cJSON *existing = cJSON_GetObjectItem(obj, key);
    if (existing) cJSON_ReplaceItemInObject(obj, key, item);
    else cJSON_AddItemToObject(obj, key, item);
}

static cJSON *json_get_or_add_object(cJSON *obj, const char *key) {
    cJSON *existing = cJSON_GetObjectItem(obj, key);
    if (existing && cJSON_IsObject(existing)) return existing;
    cJSON *created = cJSON_CreateObject();
    if (existing) cJSON_ReplaceItemInObject(obj, key, created);
    else cJSON_AddItemToObject(obj, key, created);
    return created;
}

static void load_provider_settings_from_json(cJSON *json, const char *provider,
                                             GooseConfig *cfg, int override_model,
                                             int override_base, int override_key) {
    cJSON *profiles = json_get_object(json, "provider_profiles");
    cJSON *entry = profiles ? json_get_object(profiles, provider) : NULL;
    if (!entry) return;

    const char *v;
    if (override_model && (v = json_get_string(entry, "model"))) { free(cfg->model); cfg->model = strdup(v); }
    if (override_base && (v = json_get_string(entry, "base_url"))) { free(cfg->base_url); cfg->base_url = strdup(v); }
    if (override_key && (v = json_get_string(entry, "api_key"))) { if (cfg->api_key) free(cfg->api_key); cfg->api_key = strdup(v); }
}

GooseConfig config_load(void) {
    GooseConfig cfg = {0};
    cfg.permission_mode = PERM_ALLOW;
    cfg.max_tokens = 8192;
    cfg.temperature = 0.7;
    cfg.max_turns = 64;
    cfg.context_window = 128000;
    cfg.verbose = 0;

    char *home = config_get_home_dir();
    cfg.working_dir = getcwd(NULL, 0);
    if (!cfg.working_dir) cfg.working_dir = strdup(".");

    cfg.session_dir = config_path(home, ".goosecode/sessions");
    cfg.session_memory_dir = config_path(home, ".goosecode/session-memory");
    cfg.tool_result_dir = config_path(home, ".goosecode/tool-results");
    cfg.subagent_dir = config_path(home, ".goosecode/subagents");
    cfg.worktree_dir = config_path(home, ".goosecode/worktrees");
    char *root_dir = config_path(home, ".goosecode");
    ensure_dir(root_dir);
    free(root_dir);
    ensure_dir(cfg.session_dir);
    ensure_dir(cfg.session_memory_dir);
    ensure_dir(cfg.tool_result_dir);
    ensure_dir(cfg.subagent_dir);
    ensure_dir(cfg.worktree_dir);

    cfg.todo_store = config_path(home, ".goosecode/todos.json");
    cfg.skill_dir = config_path(home, ".goosecode/skills");
    ensure_dir(cfg.skill_dir);

    const char *env_provider = getenv("GOOSECODE_PROVIDER");
    if (env_provider) cfg.provider = strdup(env_provider);
    else cfg.provider = strdup("openai");

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
    const char *env_ctx = getenv("GOOSECODE_CONTEXT_WINDOW");
    if (env_ctx) cfg.context_window = atoi(env_ctx);

    cJSON *mcp = cJSON_CreateArray();
    cfg.mcp_servers = mcp;
    cfg.allowed_tools = cJSON_CreateArray();
    cfg.denied_tools = cJSON_CreateArray();

    char *proj_settings = config_path(cfg.working_dir, ".goosecode/settings.json");
    cJSON *proj = load_json_file(proj_settings);
    if (proj) {
        const char *v;
        v = json_get_string(proj, "provider"); if (v && !env_provider) { free(cfg.provider); cfg.provider = strdup(v); }
        v = json_get_string(proj, "model"); if (v) { free(cfg.model); cfg.model = strdup(v); }
        v = json_get_string(proj, "system_prompt"); if (v) { free(cfg.system_prompt); cfg.system_prompt = strdup(v); }
        v = json_get_string(proj, "append_system_prompt"); if (v) { free(cfg.append_system_prompt); cfg.append_system_prompt = strdup(v); }
        v = json_get_string(proj, "override_system_prompt"); if (v) { free(cfg.override_system_prompt); cfg.override_system_prompt = strdup(v); }
        v = json_get_string(proj, "output_style"); if (v) { free(cfg.output_style); cfg.output_style = strdup(v); }
        v = json_get_string(proj, "response_language"); if (v) { free(cfg.response_language); cfg.response_language = strdup(v); }
        v = json_get_string(proj, "base_url"); if (v) { free(cfg.base_url); cfg.base_url = strdup(v); }
        v = json_get_string(proj, "permission_mode"); if (v) cfg.permission_mode = config_perm_mode_from_str(v);
        cfg.max_tokens = json_get_int(proj, "max_tokens", cfg.max_tokens);
        cfg.max_turns = json_get_int(proj, "max_turns", cfg.max_turns);
        cfg.context_window = json_get_int(proj, "context_window", cfg.context_window);
        cJSON *ms = json_get_array(proj, "mcp_servers");
        if (ms) {
            cJSON_Delete(cfg.mcp_servers);
            cfg.mcp_servers = cJSON_CreateArray();
            merge_mcp_servers(&cfg.mcp_servers, ms);
        }
        cJSON *mt = json_get_array(proj, "allowed_tools");
        if (mt) { cJSON_Delete(cfg.allowed_tools); cfg.allowed_tools = cJSON_Duplicate(mt, 1); }
        cJSON *dt = json_get_array(proj, "denied_tools");
        if (dt) { cJSON_Delete(cfg.denied_tools); cfg.denied_tools = cJSON_Duplicate(dt, 1); }
        load_provider_settings_from_json(proj, cfg.provider, &cfg, !env_model, !env_base, !env_key);
        cJSON_Delete(proj);
    }
    free(proj_settings);

    char *user_settings = config_path(home, ".goosecode/settings.json");
    cJSON *user = load_json_file(user_settings);
    if (user) {
        const char *v;
        v = json_get_string(user, "provider"); if (v && !env_provider) { free(cfg.provider); cfg.provider = strdup(v); }
        v = json_get_string(user, "model"); if (v && !env_model) { free(cfg.model); cfg.model = strdup(v); }
        v = json_get_string(user, "system_prompt"); if (v) { free(cfg.system_prompt); cfg.system_prompt = strdup(v); }
        v = json_get_string(user, "append_system_prompt"); if (v) { free(cfg.append_system_prompt); cfg.append_system_prompt = strdup(v); }
        v = json_get_string(user, "override_system_prompt"); if (v) { free(cfg.override_system_prompt); cfg.override_system_prompt = strdup(v); }
        v = json_get_string(user, "output_style"); if (v) { free(cfg.output_style); cfg.output_style = strdup(v); }
        v = json_get_string(user, "response_language"); if (v) { free(cfg.response_language); cfg.response_language = strdup(v); }
        v = json_get_string(user, "base_url"); if (v && !env_base) { free(cfg.base_url); cfg.base_url = strdup(v); }
        v = json_get_string(user, "api_key"); if (v) { if (cfg.api_key) free(cfg.api_key); cfg.api_key = strdup(v); }
        v = json_get_string(user, "permission_mode"); if (v && !env_perms) cfg.permission_mode = config_perm_mode_from_str(v);
        cfg.max_tokens = json_get_int(user, "max_tokens", cfg.max_tokens);
        cfg.max_turns = json_get_int(user, "max_turns", cfg.max_turns);
        cfg.context_window = json_get_int(user, "context_window", cfg.context_window);
        cJSON *ms = json_get_array(user, "mcp_servers");
        if (ms) merge_mcp_servers(&cfg.mcp_servers, ms);
        load_provider_settings_from_json(user, cfg.provider, &cfg, !env_model, !env_base, !env_key);
        cJSON_Delete(user);
    }
    free(user_settings);
    free(home);
    const ProviderProfile *profile = provider_profile_detect(&cfg);
    if (profile) {
        free(cfg.provider);
        cfg.provider = strdup(profile->name);
    }
    return cfg;
}

int config_save_user_settings(const GooseConfig *cfg) {
    char *path = config_user_settings_path();
    cJSON *json = load_json_file(path);
    if (!json || !cJSON_IsObject(json)) {
        if (json) cJSON_Delete(json);
        json = cJSON_CreateObject();
    }

    json_set_string(json, "provider", cfg->provider ? cfg->provider : provider_profile_detect(cfg)->name);
    json_set_string(json, "base_url", cfg->base_url ? cfg->base_url : "");
    json_set_string(json, "model", cfg->model ? cfg->model : "");
    if (cfg->output_style && cfg->output_style[0]) {
        json_set_string(json, "output_style", cfg->output_style);
    }
    if (cfg->response_language && cfg->response_language[0]) {
        json_set_string(json, "response_language", cfg->response_language);
    }
    if (cfg->api_key && cfg->api_key[0]) {
        json_set_string(json, "api_key", cfg->api_key);
    }
    cJSON *profiles = json_get_or_add_object(json, "provider_profiles");
    const char *provider_name = cfg->provider ? cfg->provider : provider_profile_detect(cfg)->name;
    cJSON *entry = json_get_or_add_object(profiles, provider_name);
    json_set_string(entry, "base_url", cfg->base_url ? cfg->base_url : "");
    json_set_string(entry, "model", cfg->model ? cfg->model : "");
    if (cfg->api_key && cfg->api_key[0]) {
        json_set_string(entry, "api_key", cfg->api_key);
    }

    int rc = json_write_file(path, json);
    cJSON_Delete(json);
    free(path);
    return rc;
}

int config_load_user_provider_settings(const char *provider, char **base_url_out,
                                       char **model_out, char **api_key_out) {
    char *path = config_user_settings_path();
    cJSON *json = load_json_file(path);
    free(path);
    if (!json) return -1;

    cJSON *profiles = json_get_object(json, "provider_profiles");
    cJSON *entry = profiles ? json_get_object(profiles, provider) : NULL;
    if (!entry) {
        cJSON_Delete(json);
        return -1;
    }

    const char *v;
    if (base_url_out && (v = json_get_string(entry, "base_url"))) *base_url_out = strdup(v);
    if (model_out && (v = json_get_string(entry, "model"))) *model_out = strdup(v);
    if (api_key_out && (v = json_get_string(entry, "api_key"))) *api_key_out = strdup(v);
    cJSON_Delete(json);
    return 0;
}

void config_free(GooseConfig *cfg) {
    free(cfg->provider);
    free(cfg->base_url);
    free(cfg->api_key);
    free(cfg->model);
    free(cfg->system_prompt);
    free(cfg->append_system_prompt);
    free(cfg->override_system_prompt);
    free(cfg->output_style);
    free(cfg->response_language);
    free(cfg->working_dir);
    free(cfg->session_dir);
    free(cfg->session_memory_dir);
    free(cfg->tool_result_dir);
    free(cfg->subagent_dir);
    free(cfg->worktree_dir);
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
    return PERM_ALLOW;
}
