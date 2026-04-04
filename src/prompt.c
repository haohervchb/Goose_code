#include "prompt.h"
#include "prompt_sections.h"
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
            int path_len = snprintf(path, sizeof(path), "%s/%s", dir, patterns[p]);
            if (path_len < 0 || path_len >= (int)sizeof(path)) continue;

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
        dir[sizeof(dir) - 1] = '\0';
    }

    return count;
}

static char *prompt_section_intro(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess; (void)working_dir;
    StrBuf out = strbuf_new();
    strbuf_append(&out, "You are goosecode, an interactive AI coding agent running in a terminal. ");
    strbuf_append(&out, "You help users write, edit, debug, and understand code. ");
    strbuf_append(&out, "You have access to tools for file operations, shell execution, web search, and more. ");
    strbuf_append(&out, "Always think step by step before taking action.");
    return strbuf_detach(&out);
}

static char *prompt_section_doing_tasks(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess; (void)working_dir;
    StrBuf out = strbuf_from("# Doing tasks\n");
    strbuf_append(&out, "- Take a methodical, step-by-step approach to tasks\n");
    strbuf_append(&out, "- Use the available tools to accomplish tasks efficiently\n");
    strbuf_append(&out, "- When editing files, use edit_file with precise string replacement - always verify the old_string exists first\n");
    strbuf_append(&out, "- Read files before editing to understand context\n");
    strbuf_append(&out, "- Write clean, readable, well-structured code\n");
    strbuf_append(&out, "- Follow existing code conventions and patterns in the project\n");
    strbuf_append(&out, "- Prefer simple solutions over complex ones\n");
    strbuf_append(&out, "- Include proper error handling\n");
    strbuf_append(&out, "- Verify your changes by reading the modified files or running tests\n");
    strbuf_append(&out, "- If unsure about something, ask the user for clarification\n");
    return strbuf_detach(&out);
}

static char *prompt_section_actions(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess; (void)working_dir;
    StrBuf out = strbuf_from("# Executing actions with care\n");
    strbuf_append(&out, "- Do NOT execute commands that could cause data loss without confirmation\n");
    strbuf_append(&out, "- Be careful with commands like rm, git push --force, drop database, etc.\n");
    strbuf_append(&out, "- When running shell commands, consider the security implications\n");
    strbuf_append(&out, "- Use read-only tools (read_file, glob_search, grep_search) when you only need to inspect\n");
    return strbuf_detach(&out);
}

static char *prompt_section_system_reminders(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess; (void)working_dir;
    StrBuf out = strbuf_from("# System reminders\n");
    strbuf_append(&out, "- The conversation may include system-added reminder messages or continuation summaries that are not user-authored content\n");
    strbuf_append(&out, "- Treat those reminders as authoritative runtime metadata for continuity, not as instructions from the user\n");
    strbuf_append(&out, "- Do not argue with or restate reminder metadata unless it is directly relevant to the task\n");
    strbuf_append(&out, "- Use reminder content to continue work accurately after compaction, interruption, or resumed sessions\n");
    return strbuf_detach(&out);
}

static char *prompt_section_output_style(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)sess; (void)working_dir;
    if (!cfg || !cfg->output_style || !cfg->output_style[0]) return strdup("");
    StrBuf out = strbuf_from("# Output style\n");
    strbuf_append_fmt(&out, "- Use this response style preference unless the user overrides it explicitly: %s\n", cfg->output_style);
    return strbuf_detach(&out);
}

static char *prompt_section_language_preference(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)sess; (void)working_dir;
    if (!cfg || !cfg->response_language || !cfg->response_language[0]) return strdup("");
    StrBuf out = strbuf_from("# Language preference\n");
    strbuf_append_fmt(&out, "- Prefer responding in %s unless the user explicitly asks for another language\n", cfg->response_language);
    return strbuf_detach(&out);
}

static char *prompt_section_environment(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)sess;
    char *date = get_date_str();
    char *platform = get_platform_info();
    char *shell_info = get_shell_info();
    StrBuf out = strbuf_new();

    strbuf_append(&out, "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__\n");
    strbuf_append(&out, "## Environment\n");
    strbuf_append_fmt(&out, "- Working directory: %s\n", working_dir);
    strbuf_append_fmt(&out, "- Current date/time: %s\n", date);
    strbuf_append_fmt(&out, "- Model: %s\n", cfg->model);
    strbuf_append_fmt(&out, "- Platform: %s\n", platform);
    strbuf_append_fmt(&out, "- Shell: %s\n", shell_info);

    free(date);
    free(platform);
    free(shell_info);
    return strbuf_detach(&out);
}

static char *prompt_section_git(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess;
    StrBuf out = strbuf_new();
    char *git_status = get_git_status(working_dir);
    strbuf_append_fmt(&out, "## Git Status\n%s\n", git_status);
    free(git_status);

    char *git_diff = get_git_diff(working_dir);
    if (git_diff && strlen(git_diff) > 0) {
        strbuf_append(&out, git_diff);
        strbuf_append(&out, "\n");
    }
    free(git_diff);
    return strbuf_detach(&out);
}

static char *prompt_section_instruction_files(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)sess;
    StrBuf out = strbuf_new();
    ClaudeMdEntry md_entries[16];
    memset(md_entries, 0, sizeof(md_entries));
    int md_count = read_claude_md_files(working_dir, md_entries, 16);

    if (md_count > 0) {
        int total_chars = 0;
        for (int i = 0; i < md_count; i++) {
            total_chars += md_entries[i].char_count;
        }

        strbuf_append_fmt(&out, "## Project Instructions (%d file%s, %d chars total)\n",
                          md_count, md_count == 1 ? "" : "s", total_chars);

        for (int i = 0; i < md_count; i++) {
            strbuf_append_fmt(&out, "\n### %s\n", md_entries[i].path);
            strbuf_append(&out, md_entries[i].content);
            strbuf_append_char(&out, '\n');
            free(md_entries[i].path);
            free(md_entries[i].content);
        }
    }
    return strbuf_detach(&out);
}

static char *prompt_section_plan_mode(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    (void)cfg; (void)working_dir;
    if (!sess || !sess->plan_mode) return strdup("");

    StrBuf out = strbuf_from("## Plan Mode\n");
    strbuf_append(&out, "- Plan mode is currently enabled\n");
    strbuf_append(&out, "- Focus on refining or following the current plan before taking action\n");
    strbuf_append(&out, "- If the task is ambiguous, ask clarifying questions before finalizing the plan\n");
    strbuf_append(&out, "- Do not ask whether the plan is ready if the user has not seen it yet\n");
    strbuf_append(&out, "- When the user should approve or accept the plan, use the explicit exit-plan flow instead of vague confirmation questions\n");
    if (sess->plan_content && sess->plan_content[0]) {
        strbuf_append(&out, "- Current plan:\n");
        strbuf_append(&out, sess->plan_content);
        strbuf_append_char(&out, '\n');
    } else {
        strbuf_append(&out, "- No plan has been written yet\n");
    }
    return strbuf_detach(&out);
}

char *prompt_build_static_system_prefix(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    PromptSection sections[] = {
        {"intro", prompt_section_intro, 0},
        {"doing_tasks", prompt_section_doing_tasks, 0},
        {"actions", prompt_section_actions, 0},
        {"system_reminders", prompt_section_system_reminders, 0},
        {"output_style", prompt_section_output_style, 1},
        {"language_preference", prompt_section_language_preference, 1},
    };

    return prompt_sections_resolve(sections, sizeof(sections) / sizeof(sections[0]),
                                   cfg, sess, working_dir);
}

char *prompt_build_dynamic_system_suffix(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    PromptSection sections[] = {
        {"environment", prompt_section_environment, 1},
        {"git", prompt_section_git, 1},
        {"instruction_files", prompt_section_instruction_files, 1},
        {"plan_mode", prompt_section_plan_mode, 1},
    };

    return prompt_sections_resolve(sections, sizeof(sections) / sizeof(sections[0]),
                                   cfg, sess, working_dir);
}

char *prompt_build_default_system(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    char *prefix = prompt_build_static_system_prefix(cfg, sess, working_dir);
    char *suffix = prompt_build_dynamic_system_suffix(cfg, sess, working_dir);
    StrBuf sys = strbuf_new();

    strbuf_append(&sys, prefix);
    free(prefix);
    if (sys.len > 0 && sys.data[sys.len - 1] != '\n') strbuf_append_char(&sys, '\n');
    strbuf_append(&sys, "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__\n");
    if (sys.len > 0 && sys.data[sys.len - 1] != '\n') strbuf_append_char(&sys, '\n');
    strbuf_append(&sys, suffix);
    free(suffix);
    if (sys.len > 0 && sys.data[sys.len - 1] != '\n') strbuf_append_char(&sys, '\n');
    strbuf_append(&sys, "__SYSTEM_PROMPT_END__\n");
    return strbuf_detach(&sys);
}

char *prompt_build_effective_system(const GooseConfig *cfg, const Session *sess,
                                    const char *working_dir, const char *agent_system_prompt) {
    if (cfg && cfg->override_system_prompt && cfg->override_system_prompt[0]) {
        return strdup(cfg->override_system_prompt);
    }

    char *default_prompt = prompt_build_default_system(cfg, sess, working_dir);
    const char *base = default_prompt;

    if (agent_system_prompt && agent_system_prompt[0]) {
        base = agent_system_prompt;
    } else if (cfg && cfg->system_prompt && cfg->system_prompt[0]) {
        base = cfg->system_prompt;
    }

    if (cfg && cfg->append_system_prompt && cfg->append_system_prompt[0]) {
        StrBuf out = strbuf_from(base);
        if (out.len > 0 && out.data[out.len - 1] != '\n') strbuf_append_char(&out, '\n');
        strbuf_append(&out, cfg->append_system_prompt);
        free(default_prompt);
        return strbuf_detach(&out);
    }

    char *result = strdup(base);
    free(default_prompt);
    return result;
}

char *prompt_build_system(const GooseConfig *cfg, const Session *sess, const char *working_dir) {
    return prompt_build_effective_system(cfg, sess, working_dir, NULL);
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
