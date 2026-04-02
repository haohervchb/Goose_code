#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *run_git_capture(const GooseConfig *cfg, const char *cmd_suffix) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>&1", cfg->working_dir, cmd_suffix);

    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("Error: failed to run git command\n");

    StrBuf out = strbuf_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        strbuf_append(&out, buf);
    }

    int rc = pclose(fp);
    if (out.len == 0) {
        strbuf_append(&out, rc == 0 ? "Done.\n" : "Git command failed.\n");
    }
    return strbuf_detach(&out);
}

static char *cmd_branch_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;

    if (!args || !args[0] || strcmp(args, "show") == 0) {
        return run_git_capture(cfg, "branch --show-current");
    }
    if (strcmp(args, "list") == 0) {
        return run_git_capture(cfg, "branch --list");
    }
    if (strncmp(args, "create ", 7) == 0) {
        StrBuf cmd = strbuf_new();
        strbuf_append_fmt(&cmd, "checkout -b \"%s\"", args + 7);
        char *result = run_git_capture(cfg, cmd.data);
        strbuf_free(&cmd);
        return result;
    }
    if (strncmp(args, "switch ", 7) == 0) {
        StrBuf cmd = strbuf_new();
        strbuf_append_fmt(&cmd, "checkout \"%s\"", args + 7);
        char *result = run_git_capture(cfg, cmd.data);
        strbuf_free(&cmd);
        return result;
    }

    return strdup(
        "Usage:\n"
        "/branch\n"
        "/branch show\n"
        "/branch list\n"
        "/branch create <name>\n"
        "/branch switch <name>\n");
}

void cmd_branch_register(CommandRegistry *reg) {
    Command cmd = {strdup("branch"), strdup("Show, list, create, or switch git branches"), 1, cmd_branch_exec};
    command_registry_register(reg, cmd);
}
