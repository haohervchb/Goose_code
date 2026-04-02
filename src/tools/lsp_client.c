#include "tools/lsp_client.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pid_t pid;
    FILE *in;
    FILE *out;
} LspProcess;

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static char *path_join(const char *base, const char *path) {
    size_t len = strlen(base) + strlen(path) + 2;
    char *out = malloc(len);
    snprintf(out, len, "%s/%s", base, path);
    return out;
}

static char *resolve_path(const GooseConfig *cfg, const char *path) {
    if (!path || !path[0]) return NULL;
    if (path[0] == '/') return strdup(path);
    if (cfg && cfg->working_dir && cfg->working_dir[0]) return path_join(cfg->working_dir, path);
    return strdup(path);
}

static char *path_to_uri(const char *path) {
    size_t len = strlen(path) + 8;
    char *uri = malloc(len);
    snprintf(uri, len, "file://%s", path);
    return uri;
}

static const char *language_id_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".rs") == 0) return "rust";
    return NULL;
}

static char *default_server_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".rs") == 0) return strdup("rust-analyzer");
    return NULL;
}

static char **build_argv(cJSON *json, const char *file_path, int *argc_out) {
    const char *server_command = json_get_string(json, "server_command");
    char *default_command = NULL;
    if (!server_command || !server_command[0]) {
        default_command = default_server_for_path(file_path);
        server_command = default_command;
    }
    if (!server_command || !server_command[0]) {
        free(default_command);
        return NULL;
    }

    cJSON *server_args = json_get_array(json, "server_args");
    int argc = 1 + (server_args ? cJSON_GetArraySize(server_args) : 0);
    char **argv = calloc((size_t)argc + 1, sizeof(char *));
    argv[0] = strdup(server_command);
    for (int i = 1; i < argc; i++) {
        cJSON *arg = cJSON_GetArrayItem(server_args, i - 1);
        if (!cJSON_IsString(arg) || !arg->valuestring) {
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            free(default_command);
            return NULL;
        }
        argv[i] = strdup(arg->valuestring);
    }
    *argc_out = argc;
    free(default_command);
    return argv;
}

static void free_argv(char **argv, int argc) {
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

static char *lsp_spawn(cJSON *json, const char *file_path, LspProcess *proc) {
    memset(proc, 0, sizeof(*proc));
    int argc = 0;
    char **argv = build_argv(json, file_path, &argc);
    if (!argv) return strdup("Error: unable to determine LSP server command for this file type");

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        free_argv(argv, argc);
        return strdup("Error: failed to create LSP pipes");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        free_argv(argv, argc);
        return strdup("Error: failed to fork LSP server");
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
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
    free_argv(argv, argc);
    if (!proc->in || !proc->out) {
        if (proc->in) fclose(proc->in); else close(to_child[1]);
        if (proc->out) fclose(proc->out); else close(from_child[0]);
        waitpid(pid, NULL, 0);
        return strdup("Error: failed to open LSP stdio streams");
    }
    return NULL;
}

static void lsp_close(LspProcess *proc) {
    if (proc->in) fclose(proc->in);
    if (proc->out) fclose(proc->out);
    if (proc->pid > 0) waitpid(proc->pid, NULL, 0);
    memset(proc, 0, sizeof(*proc));
}

static char *send_json(FILE *out, const cJSON *msg) {
    char *body = json_to_string(msg);
    if (!body) return strdup("Error: failed to encode LSP request");
    int rc = fprintf(out, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    free(body);
    if (rc < 0 || fflush(out) != 0) return strdup("Error: failed to write LSP request");
    return NULL;
}

static char *read_message(FILE *in, cJSON **json_out) {
    *json_out = NULL;
    char line[512];
    int content_length = -1;
    int saw_header = 0;

    while (fgets(line, sizeof(line), in)) {
        if (strcmp(line, "\n") == 0 || strcmp(line, "\r\n") == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atoi(line + 15);
            saw_header = 1;
        } else if (!saw_header) {
            line[strcspn(line, "\r\n")] = '\0';
            StrBuf out = strbuf_from("Error: language server did not speak LSP");
            if (line[0]) {
                strbuf_append_fmt(&out, "; first output: %s", line);
            }
            return strbuf_detach(&out);
        }
    }

    if (content_length <= 0) return strdup("Error: invalid LSP response headers");

    char *body = malloc((size_t)content_length + 1);
    if (!body) return strdup("Error: out of memory");
    size_t got = fread(body, 1, (size_t)content_length, in);
    body[got] = '\0';
    if (got != (size_t)content_length) {
        free(body);
        return strdup("Error: incomplete LSP response body");
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return strdup("Error: failed to parse LSP response");
    *json_out = json;
    return NULL;
}

static char *request_response(LspProcess *proc, cJSON *req, int expect_id, cJSON **response_out) {
    *response_out = NULL;
    char *err = send_json(proc->in, req);
    if (err) return err;

    for (;;) {
        cJSON *resp = NULL;
        err = read_message(proc->out, &resp);
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

static char *lsp_initialize(LspProcess *proc, const char *root_uri) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", 1);
    cJSON_AddStringToObject(req, "method", "initialize");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "rootUri", root_uri);
    cJSON_AddNullToObject(params, "processId");
    cJSON_AddItemToObject(params, "capabilities", cJSON_CreateObject());
    cJSON_AddItemToObject(req, "params", params);

    cJSON *resp = NULL;
    char *err = request_response(proc, req, 1, &resp);
    cJSON_Delete(req);
    if (err) return err;
    cJSON_Delete(resp);

    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "initialized");
    cJSON_AddItemToObject(notif, "params", cJSON_CreateObject());
    err = send_json(proc->in, notif);
    cJSON_Delete(notif);
    return err;
}

static char *lsp_did_open(LspProcess *proc, const char *file_uri, const char *language_id, const char *text) {
    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "textDocument/didOpen");
    cJSON *params = cJSON_CreateObject();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "uri", file_uri);
    cJSON_AddStringToObject(doc, "languageId", language_id);
    cJSON_AddNumberToObject(doc, "version", 1);
    cJSON_AddStringToObject(doc, "text", text);
    cJSON_AddItemToObject(params, "textDocument", doc);
    cJSON_AddItemToObject(notif, "params", params);
    char *err = send_json(proc->in, notif);
    cJSON_Delete(notif);
    return err;
}

static cJSON *build_position_params(const char *file_uri, int line, int character) {
    cJSON *params = cJSON_CreateObject();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "uri", file_uri);
    cJSON_AddItemToObject(params, "textDocument", doc);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    cJSON_AddItemToObject(params, "position", pos);
    return params;
}

static char *extract_result(cJSON *resp, const char *action) {
    cJSON *error = json_get_object(resp, "error");
    if (error) {
        const char *msg = json_get_string(error, "message");
        if (!msg) msg = "LSP server returned an error";
        StrBuf out = strbuf_from("Error: ");
        strbuf_append(&out, msg);
        return strbuf_detach(&out);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", action);
    cJSON *payload = cJSON_GetObjectItem(resp, "result");
    if (payload) cJSON_AddItemToObject(result, "result", cJSON_Duplicate(payload, 1));
    else cJSON_AddNullToObject(result, "result");
    char *out = json_to_string(result);
    cJSON_Delete(result);
    return out ? out : strdup("Error: failed to encode LSP result");
}

char *lsp_execute_request(const GooseConfig *cfg, const char *args_json) {
    cJSON *json = cJSON_Parse(args_json);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *action = json_get_string(json, "action");
    const char *file_path_arg = json_get_string(json, "file_path");
    const char *workspace_root_arg = json_get_string(json, "workspace_root");
    int line = json_get_int(json, "line", 0);
    int character = json_get_int(json, "character", 0);

    if (!action || !action[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'action' argument required");
    }
    if (!file_path_arg || !file_path_arg[0]) {
        cJSON_Delete(json);
        return strdup("Error: 'file_path' argument required");
    }

    char *file_path = resolve_path(cfg, file_path_arg);
    if (!file_path || !file_exists(file_path)) {
        free(file_path);
        cJSON_Delete(json);
        return strdup("Error: target file does not exist");
    }

    const char *language_id = language_id_for_path(file_path);
    if (!language_id) {
        free(file_path);
        cJSON_Delete(json);
        return strdup("Error: unable to determine language id for file; provide a supported file type or extend the tool");
    }

    if ((strcmp(action, "hover") == 0 || strcmp(action, "definition") == 0) && (line < 0 || character < 0)) {
        free(file_path);
        cJSON_Delete(json);
        return strdup("Error: line and character must be non-negative");
    }

    const char *default_root = (cfg && cfg->working_dir && cfg->working_dir[0]) ? cfg->working_dir : ".";
    char *workspace_root = workspace_root_arg ? resolve_path(cfg, workspace_root_arg) : strdup(default_root);
    char *file_uri = path_to_uri(file_path);
    char *root_uri = path_to_uri(workspace_root);
    char *file_text = read_text_file(file_path);
    if (!file_text) {
        free(file_path); free(workspace_root); free(file_uri); free(root_uri);
        cJSON_Delete(json);
        return strdup("Error: failed to read file contents for LSP request");
    }

    LspProcess proc;
    char *err = lsp_spawn(json, file_path, &proc);
    if (err) {
        free(file_path); free(workspace_root); free(file_uri); free(root_uri); free(file_text);
        cJSON_Delete(json);
        return err;
    }

    err = lsp_initialize(&proc, root_uri);
    if (!err) err = lsp_did_open(&proc, file_uri, language_id, file_text);

    cJSON *req = NULL;
    if (!err) {
        req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(req, "id", 2);
        if (strcmp(action, "hover") == 0) {
            cJSON_AddStringToObject(req, "method", "textDocument/hover");
            cJSON_AddItemToObject(req, "params", build_position_params(file_uri, line, character));
        } else if (strcmp(action, "definition") == 0) {
            cJSON_AddStringToObject(req, "method", "textDocument/definition");
            cJSON_AddItemToObject(req, "params", build_position_params(file_uri, line, character));
        } else if (strcmp(action, "document_symbols") == 0) {
            cJSON_AddStringToObject(req, "method", "textDocument/documentSymbol");
            cJSON *params = cJSON_CreateObject();
            cJSON *doc = cJSON_CreateObject();
            cJSON_AddStringToObject(doc, "uri", file_uri);
            cJSON_AddItemToObject(params, "textDocument", doc);
            cJSON_AddItemToObject(req, "params", params);
        } else {
            err = strdup("Error: action must be one of hover, definition, or document_symbols");
        }
    }

    char *out = NULL;
    if (!err) {
        cJSON *resp = NULL;
        err = request_response(&proc, req, 2, &resp);
        if (!err) {
            out = extract_result(resp, action);
        }
        if (resp) cJSON_Delete(resp);
    }

    if (req) cJSON_Delete(req);
    lsp_close(&proc);
    free(file_path);
    free(workspace_root);
    free(file_uri);
    free(root_uri);
    free(file_text);
    cJSON_Delete(json);

    if (err) return err;
    return out ? out : strdup("Error: LSP request returned no result");
}
