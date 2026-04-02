#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *run_git_capture_commit(const GooseConfig *cfg, const char *cmd_suffix, int *rc_out) {
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

static int contains_secret_like_file(const char *status_output) {
    static const char *patterns[] = {
        ".env",
        "credentials.json",
        "secret",
        "secrets",
        "id_rsa",
        "id_ed25519"
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (strstr(status_output, patterns[i])) return 1;
    }
    return 0;
}

static char *shell_quote_single(const char *value) {
    size_t len = strlen(value);
    size_t extra = 2;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') extra += 3;
    }

    char *out = malloc(len + extra + 1);
    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = value[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

static char *cmd_commit_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;

    int rc = 0;
    char *status = run_git_capture_commit(cfg, "status --short", &rc);
    if (rc != 0) return status;

    if (!args || !args[0]) {
        StrBuf out = strbuf_new();
        strbuf_append(&out, "Working tree status:\n");
        strbuf_append(&out, status);
        if (strcmp(status, "Done.\n") == 0 || status[0] == '\0') {
            strbuf_append(&out, "No changes to commit.\n");
        }
        strbuf_append(&out, "Usage: /commit <message>\n");
        char *result = strbuf_detach(&out);
        free(status);
        return result;
    }

    if (strcmp(status, "Done.\n") == 0 || status[0] == '\0') {
        free(status);
        return strdup("No changes to commit.\n");
    }

    if (contains_secret_like_file(status)) {
        free(status);
        return strdup("Error: refusing to commit files that look like secrets. Review the status output manually first.\n");
    }
    free(status);

    char *quoted_dir = shell_quote_single(cfg->working_dir);
    char *quoted_msg = shell_quote_single(args);
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "git -C %s add -A && git -C %s commit -m %s 2>&1", quoted_dir, quoted_dir, quoted_msg);
    free(quoted_dir);
    free(quoted_msg);

    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("Error: failed to run git commit\n");

    StrBuf out = strbuf_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        strbuf_append(&out, buf);
    }
    rc = pclose(fp);

    if (rc != 0) {
        if (out.len == 0) strbuf_append(&out, "Git commit failed.\n");
        return strbuf_detach(&out);
    }

    char *result = strbuf_detach(&out);
    return result;
}

void cmd_commit_register(CommandRegistry *reg) {
    Command cmd = {strdup("commit"), strdup("Create a git commit from current changes"), 1, cmd_commit_exec};
    command_registry_register(reg, cmd);
}
