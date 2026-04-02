#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *run_git_capture_review(const GooseConfig *cfg, const char *cmd_suffix, int *rc_out) {
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>&1", cfg->working_dir, cmd_suffix);

    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("Error: failed to run git command\n");

    StrBuf out = strbuf_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        strbuf_append(&out, buf);
    }

    int rc = pclose(fp);
    if (rc_out) *rc_out = rc;
    if (out.len == 0) {
        strbuf_append(&out, rc == 0 ? "Done.\n" : "Git command failed.\n");
    }
    return strbuf_detach(&out);
}

static char *cmd_review_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args;
    (void)sess;

    int rc = 0;
    char *branch = run_git_capture_review(cfg, "branch --show-current", &rc);
    if (rc != 0) return branch;
    char *status = run_git_capture_review(cfg, "status --short", &rc);
    if (rc != 0) {
        free(branch);
        return status;
    }
    char *staged = run_git_capture_review(cfg, "diff --cached --stat", &rc);
    if (rc != 0) {
        free(branch);
        free(status);
        return staged;
    }
    char *unstaged = run_git_capture_review(cfg, "diff --stat", &rc);
    if (rc != 0) {
        free(branch);
        free(status);
        free(staged);
        return unstaged;
    }
    char *checks = run_git_capture_review(cfg, "diff --check", &rc);
    if (rc != 0 && checks[0] == '\0') {
        free(branch);
        free(status);
        free(staged);
        free(unstaged);
        return checks;
    }

    StrBuf out = strbuf_new();
    strbuf_append(&out, "Review summary:\n");
    strbuf_append_fmt(&out, "Current branch: %s", branch);
    if (branch[strlen(branch) - 1] != '\n') strbuf_append(&out, "\n");

    strbuf_append(&out, "\nStatus:\n");
    if (strcmp(status, "Done.\n") == 0 || status[0] == '\0') strbuf_append(&out, "Working tree clean.\n");
    else strbuf_append(&out, status);

    strbuf_append(&out, "\nStaged diff stat:\n");
    if (strcmp(staged, "Done.\n") == 0 || staged[0] == '\0') strbuf_append(&out, "No staged changes.\n");
    else strbuf_append(&out, staged);

    strbuf_append(&out, "\nUnstaged diff stat:\n");
    if (strcmp(unstaged, "Done.\n") == 0 || unstaged[0] == '\0') strbuf_append(&out, "No unstaged changes.\n");
    else strbuf_append(&out, unstaged);

    strbuf_append(&out, "\nDiff checks:\n");
    if (strcmp(checks, "Done.\n") == 0 || checks[0] == '\0') strbuf_append(&out, "No whitespace or conflict-marker issues detected.\n");
    else strbuf_append(&out, checks);

    free(branch);
    free(status);
    free(staged);
    free(unstaged);
    free(checks);
    return strbuf_detach(&out);
}

void cmd_review_register(CommandRegistry *reg) {
    Command cmd = {strdup("review"), strdup("Review current git changes locally"), 0, cmd_review_exec};
    command_registry_register(reg, cmd);
}
