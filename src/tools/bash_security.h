#ifndef BASH_SECURITY_H
#define BASH_SECURITY_H

#include <stdint.h>

typedef struct {
    int check_id;
    char *message;
    int blocked;
} BashSecurityResult;

#define BASH_CHECK_INCOMPLETE_COMMANDS    1
#define BASH_CHECK_JQ_SYSTEM_FUNCTION     2
#define BASH_CHECK_JQ_FILE_ARGUMENTS      3
#define BASH_CHECK_OBFUSCATED_FLAGS       4
#define BASH_CHECK_SHELL_METACHARACTERS   5
#define BASH_CHECK_DANGEROUS_VARIABLES    6
#define BASH_CHECK_NEWLINES               7
#define BASH_CHECK_CMD_SUBSTITUTION       8
#define BASH_CHECK_INPUT_REDIRECTION      9
#define BASH_CHECK_OUTPUT_REDIRECTION    10
#define BASH_CHECK_IFS_INJECTION         11
#define BASH_CHECK_GIT_COMMIT_SUBST      12
#define BASH_CHECK_PROC_ENVIRON_ACCESS   13
#define BASH_CHECK_MALFORMED_TOKEN       14
#define BASH_CHECK_BACKSLASH_WHITESPACE  15
#define BASH_CHECK_BRACE_EXPANSION       16
#define BASH_CHECK_CONTROL_CHARACTERS    17
#define BASH_CHECK_UNICODE_WHITESPACE    18
#define BASH_CHECK_MID_WORD_HASH         19
#define BASH_CHECK_ZSH_DANGEROUS_CMDS   20
#define BASH_CHECK_BACKSLASH_OPERATORS   21
#define BASH_CHECK_COMMENT_QUOTE_DESYNC 22
#define BASH_CHECK_QUOTED_NEWLINE        23

BashSecurityResult bash_check(const char *command);

int bash_security_is_check_enabled(int check_id);
void bash_security_init(void);

#endif
