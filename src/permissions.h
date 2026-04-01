#ifndef PERMISSIONS_H
#define PERMISSIONS_H

#include "config.h"
#include "util/cJSON.h"

typedef enum {
    PERM_CHECK_ALLOW,
    PERM_CHECK_DENY,
    PERM_CHECK_PROMPT,
    PERM_CHECK_BLOCK
} PermissionCheckResult;

PermissionCheckResult permissions_check(const GooseConfig *cfg, const char *tool_name,
                                         const char *tool_args, PermissionMode tool_required_mode);
int permissions_tool_allowed(const GooseConfig *cfg, const char *tool_name);
int permissions_tool_denied(const GooseConfig *cfg, const char *tool_name);
int permissions_tool_visible(const GooseConfig *cfg, const char *tool_name,
                             PermissionMode tool_required_mode);
const char *permissions_check_str(PermissionCheckResult r);

#endif
