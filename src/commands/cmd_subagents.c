#include "commands/commands.h"
#include "tools/subagent_store.h"
#include "util/strbuf.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int has_suffix(const char *value, const char *suffix) {
    size_t vlen = strlen(value);
    size_t slen = strlen(suffix);
    return vlen >= slen && strcmp(value + vlen - slen, suffix) == 0;
}

static char *path_join_simple(const char *a, const char *b) {
    size_t len = strlen(a) + strlen(b) + 2;
    char *out = malloc(len);
    snprintf(out, len, "%s/%s", a, b);
    return out;
}

static char *shell_quote_single_local(const char *value) {
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

static char *run_capture_cmd_local(const char *cmd, int *rc_out) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("Error: failed to run shell command\n");

    StrBuf out = strbuf_new();
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) strbuf_append(&out, buf);
    int rc = pclose(fp);
    if (rc_out) *rc_out = rc;
    if (out.len == 0) strbuf_append(&out, rc == 0 ? "Done.\n" : "Command failed.\n");
    return strbuf_detach(&out);
}

static char *git_common_dir_for_worktree(const char *worktree_path) {
    char *quoted = shell_quote_single_local(worktree_path);
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "git -C %s rev-parse --path-format=absolute --git-common-dir 2>/dev/null", quoted);
    free(quoted);

    int rc = 0;
    char *out = run_capture_cmd_local(cmd, &rc);
    if (rc != 0 || !out || out[0] == '\0' || strcmp(out, "Done.\n") == 0) {
        free(out);
        return NULL;
    }
    out[strcspn(out, "\r\n")] = '\0';
    return out;
}

static void trim_git_suffix(char *path) {
    size_t len = strlen(path);
    if (len >= 5 && strcmp(path + len - 5, "/.git") == 0) {
        path[len - 5] = '\0';
    }
}

static int remove_directory_force(const char *path) {
    char *quoted = shell_quote_single_local(path);
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);
    free(quoted);
    return system(cmd);
}

static void remove_worktree_path(const char *path) {
    char *common_dir = git_common_dir_for_worktree(path);
    if (common_dir) {
        trim_git_suffix(common_dir);
        char *quoted_root = shell_quote_single_local(common_dir);
        char *quoted_path = shell_quote_single_local(path);
        char cmd[8192];
        snprintf(cmd, sizeof(cmd), "git -C %s worktree remove --force %s >/dev/null 2>&1 && git -C %s worktree prune >/dev/null 2>&1",
                 quoted_root, quoted_path, quoted_root);
        int rc = system(cmd);
        free(quoted_root);
        free(quoted_path);
        free(common_dir);
        if (rc == 0) return;
    }
    remove_directory_force(path);
}

static char *record_path_for_id(const GooseConfig *cfg, const char *task_id) {
    size_t len = strlen(cfg->subagent_dir) + strlen(task_id) + 8;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s.json", cfg->subagent_dir, task_id);
    return path;
}

static char *cmd_subagents_list(const GooseConfig *cfg) {
    DIR *dir = opendir(cfg->subagent_dir);
    if (!dir) return strdup("No subagent records found.\n");

    StrBuf out = strbuf_from("Subagents:\n");
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_suffix(ent->d_name, ".json")) continue;
        size_t len = strlen(ent->d_name) - 5;
        char task_id[256];
        memcpy(task_id, ent->d_name, len);
        task_id[len] = '\0';

        SubagentRecord *record = subagent_record_load(cfg, task_id);
        if (!record) continue;
        strbuf_append_fmt(&out, "%s [%s|%s|%s] %s\n",
                          record->task_id,
                          record->status ? record->status : "unknown",
                          record->subagent_type ? record->subagent_type : "unknown",
                          record->workspace_mode ? record->workspace_mode : "direct",
                          record->description ? record->description : "(no description)");
        subagent_record_free(record);
        count++;
    }
    closedir(dir);
    if (count == 0) strbuf_append(&out, "(none)\n");
    return strbuf_detach(&out);
}

static char *cmd_subagents_show(const GooseConfig *cfg, const char *task_id) {
    SubagentRecord *record = subagent_record_load(cfg, task_id);
    if (!record) return strdup("Subagent not found.\n");

    StrBuf out = strbuf_from("Subagent details:\n");
    strbuf_append_fmt(&out, "task_id: %s\n", record->task_id);
    strbuf_append_fmt(&out, "status: %s\n", record->status ? record->status : "unknown");
    strbuf_append_fmt(&out, "type: %s\n", record->subagent_type ? record->subagent_type : "unknown");
    strbuf_append_fmt(&out, "workspace_mode: %s\n", record->workspace_mode ? record->workspace_mode : "direct");
    if (record->working_dir) strbuf_append_fmt(&out, "working_dir: %s\n", record->working_dir);
    if (record->description) strbuf_append_fmt(&out, "description: %s\n", record->description);
    if (record->result) strbuf_append_fmt(&out, "result: %s\n", record->result);
    if (record->error) strbuf_append_fmt(&out, "error: %s\n", record->error);

    subagent_record_free(record);
    return strbuf_detach(&out);
}

static char *cmd_subagents_clean(const GooseConfig *cfg) {
    DIR *dir = opendir(cfg->subagent_dir);
    if (!dir) return strdup("No subagent records found.\n");

    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_suffix(ent->d_name, ".json")) continue;
        size_t len = strlen(ent->d_name) - 5;
        char task_id[256];
        memcpy(task_id, ent->d_name, len);
        task_id[len] = '\0';

        SubagentRecord *record = subagent_record_load(cfg, task_id);
        if (!record) continue;
        int stale = record->status && (
            strcmp(record->status, "completed") == 0 ||
            strcmp(record->status, "error") == 0
        );
        if (stale) {
            if (record->workspace_mode && strcmp(record->workspace_mode, "git_worktree") == 0 &&
                record->working_dir && record->working_dir[0]) {
                remove_worktree_path(record->working_dir);
            }
            char *record_path = record_path_for_id(cfg, task_id);
            remove(record_path);
            free(record_path);
            removed++;
        }
        subagent_record_free(record);
    }
    closedir(dir);

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Removed %d stale subagent record(s).\n", removed);
    return strbuf_detach(&out);
}

static char *cmd_subagents_prune(const GooseConfig *cfg) {
    DIR *dir = opendir(cfg->worktree_dir);
    if (!dir) return strdup("No worktree directory found.\n");

    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char *record_path = record_path_for_id(cfg, ent->d_name);
        struct stat st;
        if (stat(record_path, &st) != 0) {
            char *worktree_path = path_join_simple(cfg->worktree_dir, ent->d_name);
            remove_worktree_path(worktree_path);
            free(worktree_path);
            removed++;
        }
        free(record_path);
    }
    closedir(dir);

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Removed %d orphaned worktree(s).\n", removed);
    return strbuf_detach(&out);
}

static char *cmd_subagents_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)sess;

    if (!args || !args[0] || strcmp(args, "list") == 0) {
        return cmd_subagents_list(cfg);
    }
    if (strncmp(args, "show ", 5) == 0) {
        return cmd_subagents_show(cfg, args + 5);
    }
    if (strcmp(args, "clean") == 0) {
        return cmd_subagents_clean(cfg);
    }
    if (strcmp(args, "prune") == 0) {
        return cmd_subagents_prune(cfg);
    }

    return strdup(
        "Usage:\n"
        "/subagents\n"
        "/subagents list\n"
        "/subagents show <task_id>\n"
        "/subagents clean\n"
        "/subagents prune\n");
}

void cmd_subagents_register(CommandRegistry *reg) {
    Command cmd = {strdup("subagents"), strdup("Inspect and clean subagent records and worktrees"), 1, cmd_subagents_exec};
    command_registry_register(reg, cmd);
}
