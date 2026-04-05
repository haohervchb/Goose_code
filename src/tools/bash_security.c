#include "bash_security.h"
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

static int has_shell_features(const char *cmd) {
    if (!cmd) return 0;
    for (const char *p = cmd; *p; p++) {
        switch (*p) {
            case '$':
                if (p[1] == '(' || p[1] == '{' || p[1] == '[') return 1;
                if ((p[1] >= 'A' && p[1] <= 'Z') || (p[1] >= 'a' && p[1] <= 'z') || p[1] == '_') return 1;
                break;
            case '|':
            case ';':
            case '>':
            case '<':
                return 1;
            case '`':
                return 1;
            case '&':
                if (p[1] == '&') return 1;
                break;
            case '=':
                if (p != cmd && ((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z') || p[-1] == '_')) {
                    return 1;
                }
                break;
        }
    }
    return 0;
}

static int is_trivial_command(const char *cmd) {
    if (!cmd) return 0;
    
    const char *trimmed = cmd;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
    
    static const char *trivial[] = {
        "ls", "ll", "la", "pwd", "echo", "date", "whoami", "id",
        "cat", "head", "tail", "wc", "sort", "uniq", "cut", "tr",
        "which", "whereis", "type", "file", "stat",
        "git status", "git log", "git show", "git diff", "git branch",
        "git remote", "git tag", "git rev-parse", "git config",
        "find .", "find /", "ls -la", "ls -l", "ls -R",
        "env", "printenv", "uname", "hostname", "uptime",
        "df", "du", "free", "top -b -n1", "ps aux"
    };
    
    size_t cmd_len = strlen(trimmed);
    for (size_t i = 0; i < sizeof(trivial) / sizeof(trivial[0]); i++) {
        size_t tlen = strlen(trivial[i]);
        if (cmd_len >= tlen && strncmp(trimmed, trivial[i], tlen) == 0) {
            char next = trimmed[tlen];
            if (next == '\0' || next == ' ' || next == '\t' || next == '\n' || next == '\r') {
                return 1;
            }
        }
    }
    return 0;
}

int bash_security_is_command_trivial(const char *command) {
    return is_trivial_command(command);
}

int bash_security_get_checks_performed(void) {
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
    
    if (is_trivial_command(command) && !has_shell_features(command)) {
        result.check_id = 0;
        result.message = strdup("Command allowed (trivial)");
        result.blocked = 0;
        return result;
    }
    
    if (has_shell_features(command)) {
        if (strstr(command, "$(") || strstr(command, "${") || strstr(command, "`")) {
            result.check_id = 8;
            result.message = strdup("Command substitution detected");
            result.blocked = 1;
            return result;
        }
        
        if (strstr(command, "LD_PRELOAD") || strstr(command, "LD_AUDIT") ||
            strstr(command, "LD_DEBUG") || strstr(command, "PATH=") ||
            strstr(command, "IFS=") || strstr(command, "ENV=")) {
            result.check_id = 6;
            result.message = strdup("Dangerous environment variable detected");
            result.blocked = 1;
            return result;
        }
        
        if (strstr(command, "BASH_FUNC_") || strstr(command, "PROMPT_COMMAND")) {
            result.check_id = 6;
            result.message = strdup("Dangerous environment variable detected");
            result.blocked = 1;
            return result;
        }
    }
    
    result.check_id = 0;
    result.message = strdup("Command allowed");
    result.blocked = 0;
    return result;
}