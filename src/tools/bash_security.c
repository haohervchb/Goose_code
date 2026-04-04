#include "bash_security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int g_enabled_checks[24] = {0};

void bash_security_init(void) {
    for (int i = 1; i <= 23; i++) {
        g_enabled_checks[i] = 1;
    }
}

int bash_security_is_check_enabled(int check_id) {
    if (check_id < 1 || check_id > 23) return 0;
    return g_enabled_checks[check_id];
}

static int contains_pattern(const char *cmd, const char *pattern) {
    return strstr(cmd, pattern) != NULL;
}

static int starts_with(const char *str, const char *prefix) {
    size_t len = strlen(prefix);
    if (strlen(str) < len) return 0;
    return strncmp(str, prefix, len) == 0;
}

static int has_dangerous_env_var(const char *cmd) {
    const char *dangerous[] = {
        "LD_PRELOAD", "LD_AUDIT", "LD_DEBUG",
        "PATH", "IFS", "ENV", "BASH_ENV",
        "BASH_FUNC_", "PROMPT_COMMAND",
        "HISTCONTROL", "HISTFILESIZE",
        "HOSTFILE"
    };
    for (size_t i = 0; i < sizeof(dangerous) / sizeof(dangerous[0]); i++) {
        char var[64];
        snprintf(var, sizeof(var), "%s=", dangerous[i]);
        if (contains_pattern(cmd, var)) return 1;
    }
    return 0;
}

static int has_cmd_substitution(const char *cmd) {
    if (contains_pattern(cmd, "$(") || contains_pattern(cmd, "${") || contains_pattern(cmd, "$[")) {
        return 1;
    }
    const char *p = cmd;
    while ((p = strstr(p, "`")) != NULL) {
        p++;
        const char *end = strchr(p, '`');
        if (end && end > p) return 1;
    }
    return 0;
}

static int has_zsh_dangerous_cmd(const char *cmd) {
    const char *zsh_dangerous[] = {
        "zmodload", "zpty", "ztcp", "zsocket",
        "sysopen", "sysread", "syswrite", "sysseek",
        "zf_rm", "zf_mv", "zf_ln", "zf_chmod",
        "emulate"
    };
    const char *cmd_start = cmd;
    while (*cmd_start == ' ' || *cmd_start == '\t') cmd_start++;
    
    for (size_t i = 0; i < sizeof(zsh_dangerous) / sizeof(zsh_dangerous[0]); i++) {
        size_t len = strlen(zsh_dangerous[i]);
        if (strncmp(cmd_start, zsh_dangerous[i], len) == 0 &&
            (cmd_start[len] == ' ' || cmd_start[len] == '\t' || cmd_start[len] == '\0')) {
            return 1;
        }
    }
    return 0;
}

static int has_control_chars(const char *cmd) {
    for (const char *p = cmd; *p; p++) {
        if ((unsigned char)*p < 32 && *p != '\t' && *p != '\n' && *p != '\r') {
            return 1;
        }
    }
    return 0;
}

static int has_unicode_whitespace(const char *cmd) {
    const char *p = cmd;
    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c == 0xC2 && (unsigned char)*(p+1) == 0xA0) return 1;
        if (c == 0xE2 && (unsigned char)*(p+1) == 0x80) {
            unsigned char c2 = (unsigned char)*(p+2);
            if (c2 == 0x8B || c2 == 0x8F || c2 == 0xA8 || c2 == 0xA9) return 1;
        }
        if (c == 0xEF && (unsigned char)*(p+1) == 0xBB && (unsigned char)*(p+2) == 0xBF) return 1;
        p++;
    }
    return 0;
}

static int has_shell_metacharacters(const char *cmd) {
    if (contains_pattern(cmd, ";&") || contains_pattern(cmd, ";&|") || contains_pattern(cmd, "||")) {
        return 1;
    }
    if (contains_pattern(cmd, "&&") && !contains_pattern(cmd, "&&&")) {
        return 1;
    }
    return 0;
}

static int has_malformed_tokens(const char *cmd) {
    int in_single = 0;
    int in_double = 0;
    const char *p = cmd;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '\\' && *(p+1)) {
            if (!in_single && (*(p+1) == '\n' || *(p+1) == ' ' || *(p+1) == '\t')) {
                return 1;
            }
        }
        p++;
    }
    return in_single || in_double;
}

static int check_incomplete_commands(const char *cmd) {
    const char *p = cmd;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-' || *p == '~') return 1;
    return 0;
}

static int check_jq_system_function(const char *cmd) {
    if (contains_pattern(cmd, "jq") && contains_pattern(cmd, "system(")) return 1;
    if (contains_pattern(cmd, "jq") && contains_pattern(cmd, "-e")) return 1;
    if (contains_pattern(cmd, "jq") && contains_pattern(cmd, "--")) return 1;
    return 0;
}

static int check_obfuscated_flags(const char *cmd) {
    const char *p = cmd;
    while (*p) {
        if (*p == '-' && *(p+1) == '-' && *(p+2) && *(p+2) != ' ') {
            return 1;
        }
        p++;
    }
    return 0;
}

static int check_shell_metacharacters_secure(const char *cmd) {
    if (contains_pattern(cmd, ";&") || contains_pattern(cmd, ";|&")) return 1;
    if (contains_pattern(cmd, ">>|") || contains_pattern(cmd, ">|")) return 1;
    return 0;
}

static int check_dangerous_variables(const char *cmd) {
    return has_dangerous_env_var(cmd);
}

static int check_newlines_outside_quotes(const char *cmd) {
    int in_single = 0;
    int in_double = 0;
    const char *p = cmd;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '\n' && !in_single && !in_double) return 1;
        p++;
    }
    return 0;
}

static int check_cmd_substitution(const char *cmd) {
    return has_cmd_substitution(cmd);
}

static int check_input_redirection(const char *cmd) {
    if (contains_pattern(cmd, "< /dev/")) return 1;
    if (contains_pattern(cmd, "<&")) return 1;
    if (contains_pattern(cmd, "<<")) return 1;
    return 0;
}

static int check_output_redirection(const char *cmd) {
    if (contains_pattern(cmd, "> /dev/")) return 1;
    if (contains_pattern(cmd, ">>|")) return 1;
    if (contains_pattern(cmd, ">&")) return 1;
    return 0;
}

static int check_ifs_injection(const char *cmd) {
    if (contains_pattern(cmd, "IFS=")) return 1;
    if (contains_pattern(cmd, "\\IFS")) return 1;
    return 0;
}

static int check_git_commit_subst(const char *cmd) {
    if (contains_pattern(cmd, "git commit") && has_cmd_substitution(cmd)) return 1;
    return 0;
}

static int check_proc_environ_access(const char *cmd) {
    if (contains_pattern(cmd, "/proc/self/environ")) return 1;
    if (contains_pattern(cmd, "/proc/")) return 1;
    return 0;
}

static int check_malformed_token(const char *cmd) {
    return has_malformed_tokens(cmd);
}

static int check_backslash_escaped_whitespace(const char *cmd) {
    const char *p = cmd;
    int in_single = 0;
    int in_double = 0;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '\\' && *(p+1) && !in_single) {
            if (*(p+1) == ' ' || *(p+1) == '\t') return 1;
        }
        p++;
    }
    return 0;
}

static int check_brace_expansion(const char *cmd) {
    if (contains_pattern(cmd, "{") && contains_pattern(cmd, "}")) return 1;
    if (contains_pattern(cmd, "..")) return 1;
    return 0;
}

static int check_control_characters(const char *cmd) {
    return has_control_chars(cmd);
}

static int check_unicode_whitespace(const char *cmd) {
    return has_unicode_whitespace(cmd);
}

static int check_mid_word_hash(const char *cmd) {
    const char *p = cmd;
    while (*p) {
        if (*p == '#') {
            if (p > cmd && *(p-1) != ' ' && *(p-1) != '\t' && *(p-1) != '\n') {
                return 1;
            }
            if (*(p+1) && *(p+1) != ' ' && *(p+1) != '\t' && *(p+1) != '\n') {
                return 1;
            }
        }
        p++;
    }
    return 0;
}

static int check_zsh_dangerous(const char *cmd) {
    return has_zsh_dangerous_cmd(cmd);
}

static int check_backslash_escaped_operators(const char *cmd) {
    if (contains_pattern(cmd, "\\;")) return 1;
    if (contains_pattern(cmd, "\\&")) return 1;
    if (contains_pattern(cmd, "\\|")) return 1;
    return 0;
}

static int check_comment_quote_desync(const char *cmd) {
    int hash_in_unquoted = 0;
    int in_single = 0;
    int in_double = 0;
    const char *p = cmd;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '#' && !in_single && !in_double) hash_in_unquoted = 1;
        else if (*p == '"' && !in_single && hash_in_unquoted) return 1;
        p++;
    }
    return 0;
}

static int check_quoted_newline(const char *cmd) {
    if (contains_pattern(cmd, "'\n'")) return 1;
    if (contains_pattern(cmd, "'\\n'")) return 1;
    return 0;
}

BashSecurityResult bash_check(const char *command) {
    BashSecurityResult result = {0, NULL, 0};
    
    if (!command || !*command) {
        result.check_id = 0;
        result.message = strdup("Empty command");
        result.blocked = 0;
        return result;
    }

    if (g_enabled_checks[BASH_CHECK_INCOMPLETE_COMMANDS]) {
        if (check_incomplete_commands(command)) {
            result.check_id = BASH_CHECK_INCOMPLETE_COMMANDS;
            result.message = strdup("Incomplete command (starts with - or ~)");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_JQ_SYSTEM_FUNCTION]) {
        if (check_jq_system_function(command)) {
            result.check_id = BASH_CHECK_JQ_SYSTEM_FUNCTION;
            result.message = strdup("jq system function or dangerous flag detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_OBFUSCATED_FLAGS]) {
        if (check_obfuscated_flags(command)) {
            result.check_id = BASH_CHECK_OBFUSCATED_FLAGS;
            result.message = strdup("Obfuscated command flags detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_SHELL_METACHARACTERS]) {
        if (check_shell_metacharacters_secure(command)) {
            result.check_id = BASH_CHECK_SHELL_METACHARACTERS;
            result.message = strdup("Dangerous shell metacharacters detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_DANGEROUS_VARIABLES]) {
        if (check_dangerous_variables(command)) {
            result.check_id = BASH_CHECK_DANGEROUS_VARIABLES;
            result.message = strdup("Dangerous environment variable detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_NEWLINES]) {
        if (check_newlines_outside_quotes(command)) {
            result.check_id = BASH_CHECK_NEWLINES;
            result.message = strdup("Newline outside quotes detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_CMD_SUBSTITUTION]) {
        if (check_cmd_substitution(command)) {
            result.check_id = BASH_CHECK_CMD_SUBSTITUTION;
            result.message = strdup("Command substitution detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_INPUT_REDIRECTION]) {
        if (check_input_redirection(command)) {
            result.check_id = BASH_CHECK_INPUT_REDIRECTION;
            result.message = strdup("Potentially dangerous input redirection");
            result.blocked = 0;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_OUTPUT_REDIRECTION]) {
        if (check_output_redirection(command)) {
            result.check_id = BASH_CHECK_OUTPUT_REDIRECTION;
            result.message = strdup("Potentially dangerous output redirection");
            result.blocked = 0;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_IFS_INJECTION]) {
        if (check_ifs_injection(command)) {
            result.check_id = BASH_CHECK_IFS_INJECTION;
            result.message = strdup("IFS injection detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_GIT_COMMIT_SUBST]) {
        if (check_git_commit_subst(command)) {
            result.check_id = BASH_CHECK_GIT_COMMIT_SUBST;
            result.message = strdup("Command substitution in git commit detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_PROC_ENVIRON_ACCESS]) {
        if (check_proc_environ_access(command)) {
            result.check_id = BASH_CHECK_PROC_ENVIRON_ACCESS;
            result.message = strdup("proc filesystem access detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_MALFORMED_TOKEN]) {
        if (check_malformed_token(command)) {
            result.check_id = BASH_CHECK_MALFORMED_TOKEN;
            result.message = strdup("Malformed token (unclosed quote)");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_BACKSLASH_WHITESPACE]) {
        if (check_backslash_escaped_whitespace(command)) {
            result.check_id = BASH_CHECK_BACKSLASH_WHITESPACE;
            result.message = strdup("Backslash-escaped whitespace detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_BRACE_EXPANSION]) {
        if (check_brace_expansion(command)) {
            result.check_id = BASH_CHECK_BRACE_EXPANSION;
            result.message = strdup("Brace expansion detected");
            result.blocked = 0;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_CONTROL_CHARACTERS]) {
        if (check_control_characters(command)) {
            result.check_id = BASH_CHECK_CONTROL_CHARACTERS;
            result.message = strdup("Control characters detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_UNICODE_WHITESPACE]) {
        if (check_unicode_whitespace(command)) {
            result.check_id = BASH_CHECK_UNICODE_WHITESPACE;
            result.message = strdup("Unicode whitespace obfuscation detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_MID_WORD_HASH]) {
        if (check_mid_word_hash(command)) {
            result.check_id = BASH_CHECK_MID_WORD_HASH;
            result.message = strdup("Mid-word hash detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_ZSH_DANGEROUS_CMDS]) {
        if (check_zsh_dangerous(command)) {
            result.check_id = BASH_CHECK_ZSH_DANGEROUS_CMDS;
            result.message = strdup("Zsh dangerous command detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_BACKSLASH_OPERATORS]) {
        if (check_backslash_escaped_operators(command)) {
            result.check_id = BASH_CHECK_BACKSLASH_OPERATORS;
            result.message = strdup("Backslash-escaped operators detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_COMMENT_QUOTE_DESYNC]) {
        if (check_comment_quote_desync(command)) {
            result.check_id = BASH_CHECK_COMMENT_QUOTE_DESYNC;
            result.message = strdup("Comment/quote state desynchronization detected");
            result.blocked = 1;
            return result;
        }
    }

    if (g_enabled_checks[BASH_CHECK_QUOTED_NEWLINE]) {
        if (check_quoted_newline(command)) {
            result.check_id = BASH_CHECK_QUOTED_NEWLINE;
            result.message = strdup("Quoted newline detected");
            result.blocked = 0;
            return result;
        }
    }

    result.check_id = 0;
    result.message = strdup("Command allowed");
    result.blocked = 0;
    return result;
}
