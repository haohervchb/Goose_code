#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int output_is_empty(const char *text) {
    return !text || text[0] == '\0' || strcmp(text, "Done.\n") == 0;
}

static int output_contains_secret_like_file(const char *text) {
    static const char *patterns[] = {
        ".env",
        "credentials.json",
        "secret",
        "secrets",
        "id_rsa",
        "id_ed25519"
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (text && strstr(text, patterns[i])) return 1;
    }
    return 0;
}

static int status_has_untracked_files(const char *status) {
    return status && strstr(status, "?? ") != NULL;
}

static int status_has_unstaged_changes(const char *status) {
    if (!status) return 0;
    const char *line = status;
    while (*line) {
        if (line[0] == ' ' && line[1] != ' ' && line[1] != '\n' && line[1] != '\r') return 1;
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    return 0;
}

static void append_review_findings(StrBuf *out, const char *status, const char *staged,
                                   const char *unstaged, const char *checks) {
    int findings = 0;

    strbuf_append(out, "Findings:\n");

    if (status_has_untracked_files(status)) {
        strbuf_append(out, "- Untracked files are present; review whether they should be included.\n");
        findings++;
    }
    if (status_has_unstaged_changes(status)) {
        strbuf_append(out, "- Unstaged changes are present; review or stage them before committing.\n");
        findings++;
    }
    if (output_is_empty(staged) && !output_is_empty(unstaged)) {
        strbuf_append(out, "- No staged changes are ready to commit yet.\n");
        findings++;
    }
    if (output_contains_secret_like_file(status)) {
        strbuf_append(out, "- Files that look like secrets are present; avoid committing them accidentally.\n");
        findings++;
    }
    if (!output_is_empty(checks)) {
        strbuf_append(out, "- git diff --check reported whitespace or conflict-marker issues.\n");
        findings++;
    }

    if (findings == 0) {
        strbuf_append(out, "- No obvious local review findings.\n");
    }
}

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

    strbuf_append(&out, "\n");
    append_review_findings(&out, status, staged, unstaged, checks);

    strbuf_append(&out, "\nStatus:\n");
    if (output_is_empty(status)) strbuf_append(&out, "Working tree clean.\n");
    else strbuf_append(&out, status);

    strbuf_append(&out, "\nStaged diff stat:\n");
    if (output_is_empty(staged)) strbuf_append(&out, "No staged changes.\n");
    else strbuf_append(&out, staged);

    strbuf_append(&out, "\nUnstaged diff stat:\n");
    if (output_is_empty(unstaged)) strbuf_append(&out, "No unstaged changes.\n");
    else strbuf_append(&out, unstaged);

    strbuf_append(&out, "\nDiff checks:\n");
    if (output_is_empty(checks)) strbuf_append(&out, "No whitespace or conflict-marker issues detected.\n");
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
