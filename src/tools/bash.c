#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

static char *run_command(const char *cmd, int timeout_sec) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return strdup("Error: pipe creation failed");

    pid_t pid = fork();
    if (pid == -1) { close(pipefd[0]); close(pipefd[1]); return strdup("Error: fork failed"); }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }

    close(pipefd[1]);

    StrBuf output = strbuf_new();
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        strbuf_append(&output, buf);
        if (output.len > 500000) break;
    }
    close(pipefd[0]);

    int status;
    int timed_out = 0;
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (waitpid(pid, &status, WNOHANG) > 0) break;
        usleep(100000);
    }
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGTERM);
        usleep(500000);
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        timed_out = 1;
    }

    if (timed_out) strbuf_append(&output, "\n[Command timed out]");
    if (WIFEXITED(status)) {
        strbuf_append_fmt(&output, "\n[Exit code: %d]", WEXITSTATUS(status));
    }

    return strbuf_detach(&output);
}

char *tool_execute_bash(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *cmd = json_get_string(json, "command");
    int timeout = json_get_int(json, "timeout", 120);
    char *cmd_copy = cmd ? strdup(cmd) : NULL;
    cJSON_Delete(json);

    if (!cmd_copy) return strdup("Error: 'command' argument required");

    char *result = run_command(cmd_copy, timeout);
    free(cmd_copy);
    return result;
}
