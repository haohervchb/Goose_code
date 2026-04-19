#include "tui_protocol.h"
#include "util/cJSON.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *tui_stdin = NULL;
static FILE *tui_stdout = NULL;

void tui_protocol_init(void) {
    tui_stdin = stdin;
    tui_stdout = stdout;
}

void tui_protocol_cleanup(void) {
    // Nothing to clean up - using stdio
}

int tui_protocol_read_request(TUIRequest *req) {
    char line[8192];
    if (!fgets(line, sizeof(line), tui_stdin)) {
        return -1;
    }
    
    // Remove trailing newline
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        line[len-1] = '\0';
    }
    
    cJSON *json = cJSON_Parse(line);
    if (!json) {
        return -1;
    }
    
    memset(req, 0, sizeof(*req));
    
    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (!type || !type->valuestring) {
        cJSON_Delete(json);
        return -1;
    }
    
    if (strcmp(type->valuestring, "init") == 0) {
        req->type = TUI_MSG_INIT;
        cJSON *wd = cJSON_GetObjectItem(json, "working_dir");
        if (wd && wd->valuestring) req->working_dir = strdup(wd->valuestring);
        cJSON *cfg = cJSON_GetObjectItem(json, "config");
        if (cfg) {
            cJSON *model = cJSON_GetObjectItem(cfg, "model");
            if (model && model->valuestring) req->model = strdup(model->valuestring);
            cJSON *provider = cJSON_GetObjectItem(cfg, "provider");
            if (provider && provider->valuestring) req->provider = strdup(provider->valuestring);
            cJSON *base_url = cJSON_GetObjectItem(cfg, "base_url");
            if (base_url && base_url->valuestring) req->base_url = strdup(base_url->valuestring);
        }
    } else if (strcmp(type->valuestring, "prompt") == 0) {
        req->type = TUI_MSG_PROMPT;
        cJSON *text = cJSON_GetObjectItem(json, "text");
        if (text && text->valuestring) req->text = strdup(text->valuestring);
    } else if (strcmp(type->valuestring, "command") == 0) {
        req->type = TUI_MSG_COMMAND;
        cJSON *name = cJSON_GetObjectItem(json, "name");
        if (name && name->valuestring) req->cmd_name = strdup(name->valuestring);
        cJSON *args = cJSON_GetObjectItem(json, "args");
        if (args && args->valuestring) req->cmd_args = strdup(args->valuestring);
    } else if (strcmp(type->valuestring, "quit") == 0) {
        req->type = TUI_MSG_QUIT;
    } else if (strcmp(type->valuestring, "ping") == 0) {
        req->type = TUI_MSG_PING;
    } else if (strcmp(type->valuestring, "request_input") == 0) {
        req->type = TUI_MSG_REQUEST_INPUT;
        cJSON *text = cJSON_GetObjectItem(json, "text");
        if (text && text->valuestring) req->text = strdup(text->valuestring);
    } else if (strcmp(type->valuestring, "response") == 0) {
        req->type = TUI_MSG_RESPONSE;
        cJSON *text = cJSON_GetObjectItem(json, "text");
        if (text && text->valuestring) req->response = strdup(text->valuestring);
    } else if (strcmp(type->valuestring, "config") == 0) {
        req->type = TUI_MSG_CONFIG;
        cJSON *cfg = cJSON_GetObjectItem(json, "config");
        if (cfg) {
            cJSON *model = cJSON_GetObjectItem(cfg, "model");
            if (model && model->valuestring) req->model = strdup(model->valuestring);
            cJSON *provider = cJSON_GetObjectItem(cfg, "provider");
            if (provider && provider->valuestring) req->provider = strdup(provider->valuestring);
            cJSON *base_url = cJSON_GetObjectItem(cfg, "base_url");
            if (base_url && base_url->valuestring) req->base_url = strdup(base_url->valuestring);
        }
    } else {
        req->type = TUI_MSG_INVALID;
    }
    
    cJSON_Delete(json);
    return 0;
}

void tui_protocol_free_request(TUIRequest *req) {
    free(req->working_dir);
    free(req->model);
    free(req->provider);
    free(req->base_url);
    free(req->text);
    free(req->cmd_name);
    free(req->cmd_args);
    free(req->response);
    memset(req, 0, sizeof(*req));
}

void tui_protocol_send_init_ok(const char *session_id, const char *session_dir,
                               const char *provider, const char *base_url, const char *model) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "init_ok");
    cJSON_AddStringToObject(json, "session_id", session_id);
    cJSON_AddStringToObject(json, "session_dir", session_dir);
    if (provider) cJSON_AddStringToObject(json, "provider", provider);
    if (base_url) cJSON_AddStringToObject(json, "base_url", base_url);
    if (model) cJSON_AddStringToObject(json, "model", model);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_response_chunk(const char *content, int done) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "response");
    cJSON_AddStringToObject(json, "content", content);
    cJSON_AddBoolToObject(json, "done", done);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_tool_start(const char *name, const char *id, const char *args_json) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "tool_start");
    cJSON_AddStringToObject(json, "name", name);
    cJSON_AddStringToObject(json, "id", id);
    cJSON *args = cJSON_Parse(args_json);
    if (args) {
        cJSON_AddItemToObject(json, "args", args);
    }
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_tool_output(const char *id, const char *output) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "tool_output");
    cJSON_AddStringToObject(json, "id", id);
    cJSON_AddStringToObject(json, "output", output);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_tool_end(const char *id, int success, const char *error) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "tool_end");
    cJSON_AddStringToObject(json, "id", id);
    cJSON_AddBoolToObject(json, "success", success);
    if (error) cJSON_AddStringToObject(json, "error", error);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_error(const char *message) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "error");
    cJSON_AddStringToObject(json, "message", message);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_request_input(const char *prompt) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "request_input");
    cJSON_AddStringToObject(json, "prompt", prompt);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_session_info(int message_count, int plan_mode) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "session_info");
    cJSON_AddNumberToObject(json, "message_count", message_count);
    cJSON_AddBoolToObject(json, "plan_mode", plan_mode);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

void tui_protocol_send_token_update(long input, long output,
                                     long cache_read, long cache_creation,
                                     int context_window) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "token_update");
    cJSON_AddNumberToObject(json, "input_tokens", input);
    cJSON_AddNumberToObject(json, "output_tokens", output);
    cJSON_AddNumberToObject(json, "cache_read_tokens", cache_read);
    cJSON_AddNumberToObject(json, "cache_creation_tokens", cache_creation);
    cJSON_AddNumberToObject(json, "context_window", context_window);
    char *str = cJSON_PrintUnformatted(json);
    fprintf(tui_stdout, "%s\n", str);
    fflush(tui_stdout);
    free(str);
    cJSON_Delete(json);
}

char *tui_protocol_read_line(const char *prompt, const char *default_value) {
    // Send prompt request to TUI
    tui_protocol_send_request_input(prompt);
    
    // Read response from TUI
    char line[8192];
    if (!fgets(line, sizeof(line), tui_stdin)) {
        return NULL;
    }
    
    // Parse the response JSON
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        line[len-1] = '\0';
    }
    
    cJSON *json = cJSON_Parse(line);
    if (!json) {
        return NULL;
    }
    
    cJSON *text = cJSON_GetObjectItem(json, "text");
    char *result = NULL;
    if (text && text->valuestring) {
        result = strdup(text->valuestring);
    }
    cJSON_Delete(json);
    
    // If response is empty and we have a default, use the default
    if ((!result || result[0] == '\0') && default_value && default_value[0]) {
        free(result);
        return strdup(default_value);
    }
    
    return result;
}

void tui_on_text(const char *text, void *ctx) {
    (void)ctx;
    tui_protocol_send_response_chunk(text, 0);
}

void tui_on_tool_start(const char *id, const char *name, const char *args, void *ctx) {
    (void)ctx;
    tui_protocol_send_tool_start(name, id, args);
}

void tui_on_tool_output(const char *id, const char *output, void *ctx) {
    (void)ctx;
    tui_protocol_send_tool_output(id, output);
}

void tui_on_tool_done(const char *id, int success, const char *error, void *ctx) {
    (void)ctx;
    tui_protocol_send_tool_end(id, success, error);
}