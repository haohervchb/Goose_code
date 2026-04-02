#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_permissions_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)sess;
    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Permission mode: %s\n", config_perm_mode_str(cfg->permission_mode));
    strbuf_append(&out, "\nModes:\n");
    strbuf_append(&out, "  read-only          - Only read-only tools allowed\n");
    strbuf_append(&out, "  workspace-write    - Read and file write tools allowed\n");
    strbuf_append(&out, "  danger-full-access - All tools except the most dangerous\n");
    strbuf_append(&out, "  prompt             - Prompt for each tool use\n");
    strbuf_append(&out, "  allow              - All tools auto-approved (default)\n");
    return strbuf_detach(&out);
}

void cmd_permissions_register(CommandRegistry *reg) {
    Command cmd = {strdup("permissions"), strdup("Show permission mode"), 0, cmd_permissions_exec};
    command_registry_register(reg, cmd);
}
