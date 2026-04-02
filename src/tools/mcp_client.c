#include "tools/mcp_client.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pid_t pid;
    FILE *in;
    FILE *out;
} McpProcess;

static cJSON *mcp_server_find(const GooseConfig *cfg, const char *server_name) {
    if (!cfg->mcp_servers) return NULL;
    cJSON *item;
    cJSON_ArrayForEach(item, cfg->mcp_servers) {
        const char *name = json_get_string(item, "name");
        if (name && strcmp(name, server_name) == 0) return item;
    }
    return NULL;
}

static char **mcp_build_argv(cJSON *server, int *argc_out) {
    const char *command = json_get_string(server, "command");
    if (!command || !command[0]) return NULL;

    cJSON *args = json_get_array(server, "args");
    int argc = 1 + (args ? cJSON_GetArraySize(args) : 0);
    char **argv = calloc((size_t)argc + 1, sizeof(char *));
    argv[0] = strdup(command);

    for (int i = 1; i < argc; i++) {
        cJSON *arg = cJSON_GetArrayItem(args, i - 1);
        if (!cJSON_IsString(arg) || !arg->valuestring) {
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            return NULL;
        }
        argv[i] = strdup(arg->valuestring);
    }

    *argc_out = argc;
    return argv;
}

static void mcp_free_argv(char **argv, int argc) {
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

static char *mcp_spawn(cJSON *server, McpProcess *proc) {
    memset(proc, 0, sizeof(*proc));

    int argc = 0;
    char **argv = mcp_build_argv(server, &argc);
    if (!argv) return strdup("Error: invalid MCP server command configuration");

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        mcp_free_argv(argv, argc);
        return strdup("Error: failed to create MCP pipes");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        mcp_free_argv(argv, argc);
        return strdup("Error: failed to fork MCP server");
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    proc->pid = pid;
    proc->in = fdopen(to_child[1], "w");
    proc->out = fdopen(from_child[0], "r");
    mcp_free_argv(argv, argc);
    if (!proc->in || !proc->out) {
        if (proc->in) fclose(proc->in); else close(to_child[1]);
        if (proc->out) fclose(proc->out); else close(from_child[0]);
        waitpid(pid, NULL, 0);
        return strdup("Error: failed to open MCP stdio streams");
    }
    return NULL;
}

static void mcp_close(McpProcess *proc) {
    if (proc->in) fclose(proc->in);
    if (proc->out) fclose(proc->out);
    if (proc->pid > 0) waitpid(proc->pid, NULL, 0);
    proc->in = NULL;
    proc->out = NULL;
    proc->pid = 0;
}

static char *mcp_send_json(FILE *out, const cJSON *msg) {
    char *body = json_to_string(msg);
    if (!body) return strdup("Error: failed to encode MCP message");
    int rc = fprintf(out, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    free(body);
    if (rc < 0 || fflush(out) != 0) return strdup("Error: failed to write MCP request");
    return NULL;
}

static char *mcp_read_message(FILE *in, cJSON **json_out) {
    *json_out = NULL;
    char line[512];
    int content_length = -1;

    while (fgets(line, sizeof(line), in)) {
        if (strcmp(line, "\n") == 0 || strcmp(line, "\r\n") == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atoi(line + 15);
        }
    }

    if (content_length <= 0) return strdup("Error: invalid MCP response headers");

    char *body = malloc((size_t)content_length + 1);
    if (!body) return strdup("Error: out of memory");
    size_t read_total = fread(body, 1, (size_t)content_length, in);
    body[read_total] = '\0';
    if (read_total != (size_t)content_length) {
        free(body);
        return strdup("Error: incomplete MCP response body");
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return strdup("Error: failed to parse MCP response");
    *json_out = json;
    return NULL;
}

static char *mcp_request_response(McpProcess *proc, cJSON *msg, int expect_id, cJSON **response_out) {
    *response_out = NULL;
    char *err = mcp_send_json(proc->in, msg);
    if (err) return err;

    for (;;) {
        cJSON *resp = NULL;
        err = mcp_read_message(proc->out, &resp);
        if (err) return err;

        cJSON *id = cJSON_GetObjectItem(resp, "id");
        if (!id || !cJSON_IsNumber(id) || id->valueint != expect_id) {
            cJSON_Delete(resp);
            continue;
        }
        *response_out = resp;
        return NULL;
    }
}

static char *mcp_initialize(McpProcess *proc) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", 1);
    cJSON_AddStringToObject(req, "method", "initialize");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(params, "capabilities", cJSON_CreateObject());
    cJSON *client = cJSON_CreateObject();
    cJSON_AddStringToObject(client, "name", "goosecode");
    cJSON_AddStringToObject(client, "version", "0.1.0");
    cJSON_AddItemToObject(params, "clientInfo", client);
    cJSON_AddItemToObject(req, "params", params);

    cJSON *resp = NULL;
    char *err = mcp_request_response(proc, req, 1, &resp);
    cJSON_Delete(req);
    if (err) return err;
    cJSON_Delete(resp);

    cJSON *notification = cJSON_CreateObject();
    cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notification, "method", "notifications/initialized");
    err = mcp_send_json(proc->in, notification);
    cJSON_Delete(notification);
    return err;
}

static char *mcp_extract_result(cJSON *resp, const char *field) {
    cJSON *error = json_get_object(resp, "error");
    if (error) {
        const char *msg = json_get_string(error, "message");
        if (msg) {
            StrBuf out = strbuf_from("Error: ");
            strbuf_append(&out, msg);
            return strbuf_detach(&out);
        }
        return strdup("Error: MCP server returned an error");
    }

    cJSON *result = json_get_object(resp, "result");
    if (!result) return strdup("Error: MCP response missing result");
    if (!field) return json_to_string(result);
    cJSON *item = cJSON_GetObjectItem(result, field);
    if (!item) return strdup("Error: MCP result missing expected field");
    return json_to_string(item);
}

char *mcp_list_resources(const GooseConfig *cfg, const char *server_name) {
    cJSON *server = mcp_server_find(cfg, server_name);
    if (!server) return strdup("Error: MCP server not found");

    McpProcess proc;
    char *err = mcp_spawn(server, &proc);
    if (err) return err;

    err = mcp_initialize(&proc);
    if (err) {
        mcp_close(&proc);
        return err;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", 2);
    cJSON_AddStringToObject(req, "method", "resources/list");
    cJSON_AddItemToObject(req, "params", cJSON_CreateObject());

    cJSON *resp = NULL;
    err = mcp_request_response(&proc, req, 2, &resp);
    cJSON_Delete(req);
    if (err) {
        mcp_close(&proc);
        return err;
    }

    char *out = mcp_extract_result(resp, "resources");
    cJSON_Delete(resp);
    mcp_close(&proc);
    return out;
}

char *mcp_read_resource(const GooseConfig *cfg, const char *server_name, const char *uri) {
    cJSON *server = mcp_server_find(cfg, server_name);
    if (!server) return strdup("Error: MCP server not found");

    McpProcess proc;
    char *err = mcp_spawn(server, &proc);
    if (err) return err;

    err = mcp_initialize(&proc);
    if (err) {
        mcp_close(&proc);
        return err;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", 2);
    cJSON_AddStringToObject(req, "method", "resources/read");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);
    cJSON_AddItemToObject(req, "params", params);

    cJSON *resp = NULL;
    err = mcp_request_response(&proc, req, 2, &resp);
    cJSON_Delete(req);
    if (err) {
        mcp_close(&proc);
        return err;
    }

    char *out = mcp_extract_result(resp, "contents");
    cJSON_Delete(resp);
    mcp_close(&proc);
    return out;
}
