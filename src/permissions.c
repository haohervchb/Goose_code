#include "permissions.h"
#include "util/json_util.h"
#include <stdio.h>
#include <string.h>

int permissions_tool_allowed(const GooseConfig *cfg, const char *tool_name) {
    if (!cfg->allowed_tools) return 0;
    cJSON *item;
    cJSON_ArrayForEach(item, cfg->allowed_tools) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, tool_name) == 0) return 1;
    }
    return 0;
}

int permissions_tool_denied(const GooseConfig *cfg, const char *tool_name) {
    if (!cfg->denied_tools) return 0;
    cJSON *item;
    cJSON_ArrayForEach(item, cfg->denied_tools) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, tool_name) == 0) return 1;
    }
    return 0;
}

PermissionCheckResult permissions_check(const GooseConfig *cfg, const char *tool_name,
                                         const char *tool_args, PermissionMode tool_required_mode) {
    (void)tool_args;

    if (permissions_tool_denied(cfg, tool_name)) return PERM_CHECK_BLOCK;
    if (permissions_tool_allowed(cfg, tool_name)) return PERM_CHECK_ALLOW;

    switch (cfg->permission_mode) {
        case PERM_ALLOW:
            return PERM_CHECK_ALLOW;
        case PERM_READ_ONLY:
            if (tool_required_mode <= PERM_READ_ONLY) return PERM_CHECK_ALLOW;
            return PERM_CHECK_BLOCK;
        case PERM_WORKSPACE_WRITE:
            if (tool_required_mode <= PERM_WORKSPACE_WRITE) return PERM_CHECK_ALLOW;
            return PERM_CHECK_PROMPT;
        case PERM_DANGER_FULL_ACCESS:
            if (tool_required_mode <= PERM_DANGER_FULL_ACCESS) return PERM_CHECK_ALLOW;
            return PERM_CHECK_PROMPT;
        case PERM_PROMPT:
        default:
            return PERM_CHECK_PROMPT;
    }
}

const char *permissions_check_str(PermissionCheckResult r) {
    switch (r) {
        case PERM_CHECK_ALLOW: return "allowed";
        case PERM_CHECK_DENY: return "denied";
        case PERM_CHECK_PROMPT: return "prompt";
        case PERM_CHECK_BLOCK: return "blocked";
        default: return "unknown";
    }
}
