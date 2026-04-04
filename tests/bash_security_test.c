#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools/bash_security.h"

int main() {
    bash_security_init();
    
    struct {
        const char *cmd;
        int should_block;
    } tests[] = {
        {"ls", 0},
        {"ls -la", 0},
        {"pwd", 0},
        {"git status", 0},
        {"cat README.md", 0},
        {"$(whoami)", 1},
        {"`id`", 1},
        {"${HOME}", 1},
        {"LD_PRELOAD=x ls", 1},
        {"PATH=/tmp ls", 1},
        {"IFS=: ls", 1},
        {"-la ls", 1},
        {"zmodload zsh/mapfile", 1},
        {"zpty -r foo", 1},
        {NULL, 0}
    };
    
    int pass = 0, fail = 0;
    
    printf("=== Bash Security Tests ===\n\n");
    
    for (int i = 0; tests[i].cmd != NULL; i++) {
        BashSecurityResult result = bash_check(tests[i].cmd);
        
        int test_pass = (result.blocked == tests[i].should_block);
        
        printf("Test: %-30s ", tests[i].cmd);
        if (test_pass) {
            printf("PASS (blocked=%d, check_id=%d)\n", result.blocked, result.check_id);
            pass++;
        } else {
            printf("FAIL (expected=%d, got=%d, check_id=%d)\n", 
                   tests[i].should_block, result.blocked, result.check_id);
            fail++;
        }
        
        free(result.message);
    }
    
    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    
    return fail > 0 ? 1 : 0;
}
