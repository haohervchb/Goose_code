#include "tools/tools.h"
#include "tools/bash_security.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

static int parse_timeout_seconds(const cJSON *json, int default_timeout) {
    cJSON *item = cJSON_GetObjectItem((cJSON *)json, "timeout");
    if (!item) return default_timeout;
    if (cJSON_IsNumber(item)) return item->valueint;
    if (cJSON_IsString(item) && item->valuestring) {
        const char *p = item->valuestring;
        if (!*p) return default_timeout;
        while (*p) {
            if (!isdigit((unsigned char)*p)) return default_timeout;
            p++;
        }
        return atoi(item->valuestring);
    }
    return default_timeout;
}

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
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    StrBuf output = strbuf_new();
    char buf[4096];
    int status;
    int timed_out = 0;
    int exited = 0;
    int ticks = timeout_sec > 0 ? timeout_sec * 10 : 1200;

    for (int i = 0; i < ticks; i++) {
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            strbuf_append(&output, buf);
            if (output.len > 500000) break;
        }

        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w > 0) {
            exited = 1;
            break;
        }
        usleep(100000);
    }

    if (!exited && waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGTERM);
        usleep(500000);
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        timed_out = 1;
    }

    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        strbuf_append(&output, buf);
        if (output.len > 500000) break;
    }
    close(pipefd[0]);

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
    int timeout = parse_timeout_seconds(json, 120);
    char *cmd_copy = cmd ? strdup(cmd) : NULL;
    cJSON_Delete(json);

    if (!cmd_copy) return strdup("Error: 'command' argument required");
    if (timeout <= 0) timeout = 120;
    if (timeout > 7200) timeout = 7200;

    BashSecurityResult sec = bash_check(cmd_copy);
    
    if (sec.blocked) {
        char *err = malloc(256);
        snprintf(err, 256, "Error: Command blocked for security: %s (check_id=%d)", sec.message, sec.check_id);
        free(sec.message);
        free(cmd_copy);
        return err;
    }
    free(sec.message);

    char *result = run_command(cmd_copy, timeout);
    free(cmd_copy);
    return result;
}
