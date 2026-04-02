#include "prompt.h"
#include "util/strbuf.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

static char *get_git_status(const char *dir) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd '%s' && git status --short --branch --no-optional-locks 2>/dev/null | head -30", dir);
    FILE *f = popen(cmd, "r");
    if (!f) return strdup("clean");
    StrBuf sb = strbuf_new();
    char buf[4096];
    size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
    buf[rd] = '\0';
    pclose(f);
    if (buf[0] == '\0') {
        strbuf_append(&sb, "clean");
    } else {
        strbuf_append(&sb, buf);
    }
    return strbuf_detach(&sb);
}

static char *get_git_diff(const char *dir) {
    char cmd[2048];
    StrBuf sb = strbuf_new();

    snprintf(cmd, sizeof(cmd), "cd '%s' && git diff --cached --no-optional-locks 2>/dev/null | head -100", dir);
    FILE *f = popen(cmd, "r");
    if (f) {
        char buf[4096];
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        buf[rd] = '\0';
        pclose(f);
        if (buf[0] != '\0') {
            strbuf_append(&sb, "\n## Staged changes:\n");
            strbuf_append(&sb, buf);
        }
    }

    snprintf(cmd, sizeof(cmd), "cd '%s' && git diff --no-optional-locks 2>/dev/null | head -100", dir);
    f = popen(cmd, "r");
    if (f) {
        char buf[4096];
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        buf[rd] = '\0';
        pclose(f);
        if (buf[0] != '\0') {
            strbuf_append(&sb, "\n## Unstaged changes:\n");
            strbuf_append(&sb, buf);
        }
    }

    return strbuf_detach(&sb);
}

static char *get_date_str(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tm);
    return strdup(buf);
}

static char *get_platform_info(void) {
    StrBuf sb = strbuf_new();
    FILE *f = popen("uname -a 2>/dev/null", "r");
    if (f) {
        char buf[512];
        if (fgets(buf, sizeof(buf), f)) strbuf_append(&sb, buf);
        pclose(f);
    }
    if (sb.len == 0) strbuf_append(&sb, "unknown");
    return strbuf_detach(&sb);
}

static char *get_shell_info(void) {
    const char *shell = getenv("SHELL");
    return shell ? strdup(shell) : strdup("/bin/sh");
}

typedef struct {
    char *path;
    char *content;
    int char_count;
} ClaudeMdEntry;

static int read_claude_md_files(const char *start_dir, ClaudeMdEntry *entries, int max_entries) {
    int count = 0;
    char dir[4096];
    strncpy(dir, start_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    const char *patterns[] = {"CLAUDE.md", "CLAUDE.local.md", ".claude/CLAUDE.md", ".claude/instructions.md", "GOOSECODE.md", NULL};

    while (count < max_entries) {
        for (int p = 0; patterns[p]; p++) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s", dir, patterns[p]);

            FILE *f = fopen(path, "r");
            if (!f) continue;

            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 4000) sz = 4000;

            char *content = malloc(sz + 1);
            size_t rd = fread(content, 1, sz, f);
            content[rd] = '\0';
            fclose(f);

            int is_dup = 0;
            for (int e = 0; e < count; e++) {
                if (entries[e].content && strcmp(entries[e].content, content) == 0) {
                    is_dup = 1;
                    free(content);
                    break;
                }
            }
            if (!is_dup) {
                entries[count].path = strdup(path);
                entries[count].content = content;
                entries[count].char_count = rd;
                count++;
            }
        }

        char *parent = dirname(dir);
        if (strcmp(parent, "/") == 0 || strcmp(parent, ".") == 0 || strcmp(parent, dir) == 0) break;
        strncpy(dir, parent, sizeof(dir) - 1);
    }

    return count;
}

char *prompt_build_system(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    StrBuf sys = strbuf_new();

    strbuf_append(&sys, "You are goosecode, an interactive AI coding agent running in a terminal. "
                         "You help users write, edit, debug, and understand code. "
                         "You have access to tools for file operations, shell execution, web search, and more. "
                         "Always think step by step before taking action.\n\n");

    strbuf_append(&sys, "# Doing tasks\n");
    strbuf_append(&sys, "- Take a methodical, step-by-step approach to tasks\n");
    strbuf_append(&sys, "- Use the available tools to accomplish tasks efficiently\n");
    strbuf_append(&sys, "- When editing files, use edit_file with precise string replacement — always verify the old_string exists first\n");
    strbuf_append(&sys, "- Read files before editing to understand context\n");
    strbuf_append(&sys, "- Write clean, readable, well-structured code\n");
    strbuf_append(&sys, "- Follow existing code conventions and patterns in the project\n");
    strbuf_append(&sys, "- Prefer simple solutions over complex ones\n");
    strbuf_append(&sys, "- Include proper error handling\n");
    strbuf_append(&sys, "- Verify your changes by reading the modified files or running tests\n");
    strbuf_append(&sys, "- If unsure about something, ask the user for clarification\n\n");

    strbuf_append(&sys, "# Executing actions with care\n");
    strbuf_append(&sys, "- Do NOT execute commands that could cause data loss without confirmation\n");
    strbuf_append(&sys, "- Be careful with commands like rm, git push --force, drop database, etc.\n");
    strbuf_append(&sys, "- When running shell commands, consider the security implications\n");
    strbuf_append(&sys, "- Use read-only tools (read_file, glob_search, grep_search) when you only need to inspect\n\n");

    char *date = get_date_str();
    char *platform = get_platform_info();
    char *shell_info = get_shell_info();

    strbuf_append_fmt(&sys, "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__\n\n");
    strbuf_append_fmt(&sys, "## Environment\n");
    strbuf_append_fmt(&sys, "- Working directory: %s\n", working_dir);
    strbuf_append_fmt(&sys, "- Current date/time: %s\n", date);
    strbuf_append_fmt(&sys, "- Model: %s\n", cfg->model);
    strbuf_append_fmt(&sys, "- Platform: %s\n", platform);
    strbuf_append_fmt(&sys, "- Shell: %s\n\n", shell_info);

    free(date);
    free(platform);
    free(shell_info);

    char *git_status = get_git_status(working_dir);
    strbuf_append_fmt(&sys, "## Git Status\n%s\n", git_status);
    free(git_status);

    char *git_diff = get_git_diff(working_dir);
    if (git_diff && strlen(git_diff) > 0) {
        strbuf_append(&sys, git_diff);
        strbuf_append(&sys, "\n");
    }
    free(git_diff);

    ClaudeMdEntry md_entries[16];
    memset(md_entries, 0, sizeof(md_entries));
    int md_count = read_claude_md_files(working_dir, md_entries, 16);

    if (md_count > 0) {
        int total_chars = 0;
        for (int i = 0; i < md_count; i++) {
            total_chars += md_entries[i].char_count;
        }

        strbuf_append_fmt(&sys, "\n## Project Instructions (%d file%s, %d chars total)\n",
                          md_count, md_count == 1 ? "" : "s", total_chars);

        for (int i = 0; i < md_count; i++) {
            strbuf_append_fmt(&sys, "\n### %s\n", md_entries[i].path);
            strbuf_append(&sys, md_entries[i].content);
            strbuf_append_char(&sys, '\n');
            free(md_entries[i].path);
            free(md_entries[i].content);
        }
    }

    if (sess && sess->plan_mode) {
        strbuf_append(&sys, "\n## Plan Mode\n");
        strbuf_append(&sys, "- Plan mode is currently enabled\n");
        strbuf_append(&sys, "- Focus on refining or following the current plan before taking action\n");
        if (sess->plan_content && sess->plan_content[0]) {
            strbuf_append(&sys, "- Current plan:\n");
            strbuf_append(&sys, sess->plan_content);
            strbuf_append_char(&sys, '\n');
        } else {
            strbuf_append(&sys, "- No plan has been written yet\n");
        }
    }

    strbuf_append(&sys, "\n__SYSTEM_PROMPT_END__\n");

    return strbuf_detach(&sys);
}

cJSON *prompt_build_user_message(const char *text) {
    return json_build_message("user", text);
}

cJSON *prompt_build_messages_with_tools(const cJSON *system_msg, const cJSON *history,
                                         const cJSON *user_msg) {
    cJSON *messages = cJSON_CreateArray();
    if (system_msg) {
        cJSON_AddItemToArray(messages, cJSON_Duplicate(system_msg, 1));
    }
    if (history) {
        cJSON *item;
        cJSON_ArrayForEach(item, history) {
            cJSON_AddItemToArray(messages, cJSON_Duplicate(item, 1));
        }
    }
    if (user_msg) cJSON_AddItemToArray(messages, cJSON_Duplicate(user_msg, 1));
    return messages;
}
