#include "api.h"
#include "util/http.h"
#include "util/sse.h"
#include "util/json_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>

ApiConfig api_config_default(void) {
    ApiConfig cfg = {0};
    cfg.base_url = getenv("OPENAI_BASE_URL");
    if (!cfg.base_url) cfg.base_url = "https://api.openai.com/v1";
    cfg.api_key = getenv("OPENAI_API_KEY");
    cfg.model = getenv("OPENAI_MODEL");
    if (!cfg.model) cfg.model = "gpt-4o";
    cfg.max_tokens = 8192;
    cfg.temperature = 0.7;
    cfg.max_retries = 3;
    return cfg;
}

void api_config_free(ApiConfig *cfg) {
    (void)cfg;
}

static char *build_request_body(const cJSON *messages, const cJSON *tools,
                                 const ApiConfig *cfg, int stream) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", cfg->model);
    cJSON_AddItemToObject(req, "messages", cJSON_Duplicate(messages, 1));
    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemToObject(req, "tools", cJSON_Duplicate(tools, 1));
    }
    cJSON_AddNumberToObject(req, "max_tokens", cfg->max_tokens);
    cJSON_AddNumberToObject(req, "temperature", cfg->temperature);
    if (stream) cJSON_AddBoolToObject(req, "stream", 1);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return body;
}

typedef struct {
    char id[256];
    char name[256];
    char *args;
    size_t args_len;
    size_t args_cap;
} PendingToolCall;

typedef struct {
    ApiStreamCallbacks *cb;
    SseParser parser;
    long *status_code;
    int initialized;
    int finish_reason_stop;
    int finish_reason_tool_calls;
    PendingToolCall pending_tool;
    char retry_after[64];
} StreamCtx;

static void pending_tool_emit(StreamCtx *sctx) {
    if (sctx->pending_tool.name[0] && sctx->cb->on_tool_call) {
        if (!sctx->pending_tool.args) {
            sctx->pending_tool.args = strdup("{}");
            sctx->pending_tool.args_len = 2;
        }
        sctx->pending_tool.args[sctx->pending_tool.args_len] = '\0';
        sctx->cb->on_tool_call(sctx->pending_tool.id, sctx->pending_tool.name,
                                sctx->pending_tool.args, sctx->cb->ctx);
    }
    sctx->pending_tool.id[0] = '\0';
    sctx->pending_tool.name[0] = '\0';
    free(sctx->pending_tool.args);
    sctx->pending_tool.args = NULL;
    sctx->pending_tool.args_len = 0;
    sctx->pending_tool.args_cap = 0;
}

static void pending_tool_append(StreamCtx *sctx, const char *data, size_t dlen) {
    PendingToolCall *pt = &sctx->pending_tool;
    if (pt->args_len + dlen + 1 > pt->args_cap) {
        pt->args_cap = (pt->args_len + dlen + 1) * 2;
        pt->args = realloc(pt->args, pt->args_cap);
    }
    memcpy(pt->args + pt->args_len, data, dlen);
    pt->args_len += dlen;
}

static void stream_chunk_cb(const char *chunk, size_t len, void *userdata) {
    StreamCtx *sctx = (StreamCtx *)userdata;
    if (!sctx->initialized) {
        sse_parser_init(&sctx->parser);
        sctx->initialized = 1;
    }

    const char *p = chunk;
    const char *end = chunk + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        if (line_len > 6 && strncmp(p, "retry: ", 7) == 0) {
            size_t rlen = line_len - 7;
            if (rlen < sizeof(sctx->retry_after) - 1) {
                memcpy(sctx->retry_after, p + 7, rlen);
                sctx->retry_after[rlen] = '\0';
            }
        }

        SseEvent ev = sse_parse_line(&sctx->parser, p, line_len);
        if (ev.type != 0 || ev.text || ev.tool_call_id || ev.error || ev.finish_reason_stop) {
            if (ev.type == SSE_EVENT_TEXT && ev.text && sctx->cb->on_text) {
                sctx->cb->on_text(ev.text, strlen(ev.text), sctx->cb->ctx);
            } else if (ev.type == SSE_EVENT_TOOL_CALL && ev.tool_call_id && ev.tool_name) {
                if (sctx->pending_tool.name[0] &&
                    strcmp(sctx->pending_tool.id, ev.tool_call_id) != 0) {
                    pending_tool_emit(sctx);
                }
                if (sctx->pending_tool.name[0] == '\0') {
                    strncpy(sctx->pending_tool.id, ev.tool_call_id, sizeof(sctx->pending_tool.id) - 1);
                    strncpy(sctx->pending_tool.name, ev.tool_name, sizeof(sctx->pending_tool.name) - 1);
                }
                if (ev.tool_args && strlen(ev.tool_args) > 0) {
                    pending_tool_append(sctx, ev.tool_args, strlen(ev.tool_args));
                }
            } else if (ev.type == SSE_EVENT_DONE && sctx->cb->on_done) {
                if (sctx->pending_tool.name[0]) {
                    pending_tool_emit(sctx);
                }
                sctx->cb->on_done(sctx->cb->ctx);
            } else if (ev.type == SSE_EVENT_ERROR && ev.error) {
                fprintf(stderr, "SSE error: %s\n", ev.error);
            }
            if (ev.finish_reason_stop) sctx->finish_reason_stop = 1;
            if (ev.finish_reason_tool_calls) sctx->finish_reason_tool_calls = 1;
            sse_event_free(&ev);
        }
        p = nl ? nl + 1 : end;
    }
}

static int parse_retry_after(const char *header_val) {
    if (!header_val) return 0;
    int sec = atoi(header_val);
    if (sec > 0 && sec < 120) return sec * 1000;
    return 0;
}

static int backoff_with_jitter(int attempt) {
    int base = 500;
    int delay = base * (1 << attempt);
    if (delay > 32000) delay = 32000;
    int jitter = (int)((double)delay * 0.25 * ((double)rand() / RAND_MAX));
    return delay + jitter;
}

ApiStatus api_chat_completions(const ApiConfig *cfg, const cJSON *messages, const cJSON *tools,
                                int stream, ApiStreamCallbacks *callbacks, ApiResponse *resp) {
    memset(resp, 0, sizeof(*resp));
    resp->text_content = strbuf_new();

    char url[1024];
    snprintf(url, sizeof(url), "%s/chat/completions", cfg->base_url);
    char *body = build_request_body(messages, tools, cfg, stream);



    int retry = 0;
    ApiStatus status = API_ERROR_NETWORK;

    while (retry <= cfg->max_retries) {
        if (retry > 0) {
            int delay = backoff_with_jitter(retry - 1);
            int retry_after_ms = parse_retry_after(resp->error);
            if (retry_after_ms > delay) delay = retry_after_ms;
            usleep(delay * 1000);
        }

        if (stream && callbacks) {
            StreamCtx sctx = {.cb = callbacks, .parser = {0}, .status_code = NULL, .initialized = 0, .finish_reason_stop = 0, .finish_reason_tool_calls = 0, .retry_after = ""};
            sse_parser_init(&sctx.parser);
            HttpResponse http_resp = http_post_stream(url, cfg->api_key, body, stream_chunk_cb, &sctx);
            resp->finish_reason_stop = sctx.finish_reason_stop;
            resp->finish_reason_tool_calls = sctx.finish_reason_tool_calls;
            sse_parser_free(&sctx.parser);
            if (http_resp.error) {
                resp->error = strdup(http_resp.error);
                status = API_ERROR_NETWORK;
            } else if (http_resp.status_code == 401) {
                resp->status = API_ERROR_AUTH;
                resp->error = strdup("Authentication failed");
                status = API_ERROR_AUTH;
            } else if (http_resp.status_code == 429) {
                resp->status = API_ERROR_RATE_LIMIT;
                resp->error = strdup("Rate limited");
                status = API_ERROR_RATE_LIMIT;
            } else if (http_resp.status_code >= 500) {
                resp->status = API_ERROR_SERVER;
                resp->error = strdup("Server error");
                status = API_ERROR_SERVER;
            } else if (http_resp.status_code == 200) {
                resp->status = API_OK;
                status = API_OK;
            } else {
                if (http_resp.body.data) {
                    fprintf(stderr, "[DEBUG] Error body (stream): %s\n", http_resp.body.data);
                }
                resp->error = malloc(128);
                snprintf(resp->error, 128, "HTTP %ld", http_resp.status_code);
                status = API_ERROR_SERVER;
            }
            http_response_free(&http_resp);
        } else {
            HttpResponse http_resp = http_post(url, cfg->api_key, "application/json", body);
            if (http_resp.error) {
                resp->error = strdup(http_resp.error);
                status = API_ERROR_NETWORK;
            } else if (http_resp.status_code == 401) {
                resp->status = API_ERROR_AUTH;
                resp->error = strdup("Authentication failed");
                status = API_ERROR_AUTH;
            } else if (http_resp.status_code == 429) {
                resp->status = API_ERROR_RATE_LIMIT;
                resp->error = strdup("Rate limited");
                status = API_ERROR_RATE_LIMIT;
            } else if (http_resp.status_code >= 500) {
                resp->status = API_ERROR_SERVER;
                resp->error = strdup("Server error");
                status = API_ERROR_SERVER;
            } else if (http_resp.status_code == 200) {
                cJSON *json = cJSON_Parse(http_resp.body.data);
                if (json) {
                    cJSON *choices = cJSON_GetObjectItem(json, "choices");
                    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                        cJSON *choice = cJSON_GetArrayItem(choices, 0);
                        cJSON *message = cJSON_GetObjectItem(choice, "message");
                        if (message) {
                            cJSON *content = cJSON_GetObjectItem(message, "content");
                            if (content && cJSON_IsString(content)) {
                                strbuf_append(&resp->text_content, content->valuestring);
                            }
                            cJSON *tc = cJSON_GetObjectItem(message, "tool_calls");
                            if (tc && cJSON_IsArray(tc)) {
                                resp->tool_calls = cJSON_Duplicate(tc, 1);
                            }
                        }
                        cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");
                        if (finish && cJSON_IsString(finish)) {
                            if (strcmp(finish->valuestring, "stop") == 0) resp->finish_reason_stop = 1;
                            if (strcmp(finish->valuestring, "tool_calls") == 0) resp->finish_reason_tool_calls = 1;
                        }
                    }
                    cJSON *usage = cJSON_GetObjectItem(json, "usage");
                    if (usage) {
                        resp->input_tokens = json_get_int(usage, "prompt_tokens", 0);
                        resp->output_tokens = json_get_int(usage, "completion_tokens", 0);
                        resp->cache_read_tokens = json_get_int(usage, "cache_read_input_tokens", 0);
                        resp->cache_creation_tokens = json_get_int(usage, "cache_creation_input_tokens", 0);
                    }
                    cJSON *sys_fingerprint = cJSON_GetObjectItem(json, "system_fingerprint");
                    if (sys_fingerprint && cJSON_IsString(sys_fingerprint)) {
                        resp->system_fingerprint = strdup(sys_fingerprint->valuestring);
                    }
                    cJSON_Delete(json);
                }
                resp->status = API_OK;
                status = API_OK;
            } else {
                if (http_resp.body.data) {
                    fprintf(stderr, "[DEBUG] Error body (non-stream): %s\n", http_resp.body.data);
                }
                resp->error = malloc(128);
                snprintf(resp->error, 128, "HTTP %ld", http_resp.status_code);
                status = API_ERROR_SERVER;
            }
            http_response_free(&http_resp);
        }

        if (status == API_OK) break;
        if (status == API_ERROR_AUTH) break;
        retry++;
    }

    resp->status = status;
    free(body);
    return status;
}

ApiResponse api_send_message(const ApiConfig *cfg, const cJSON *messages, const cJSON *tools) {
    ApiResponse resp;
    api_chat_completions(cfg, messages, tools, 0, NULL, &resp);
    return resp;
}

void api_response_free(ApiResponse *resp) {
    strbuf_free(&resp->text_content);
    if (resp->tool_calls) cJSON_Delete(resp->tool_calls);
    free(resp->error);
    free(resp->system_fingerprint);
    resp->error = NULL;
    resp->system_fingerprint = NULL;
}

const char *api_status_str(ApiStatus s) {
    switch (s) {
        case API_OK: return "ok";
        case API_ERROR_NETWORK: return "network error";
        case API_ERROR_AUTH: return "authentication error";
        case API_ERROR_RATE_LIMIT: return "rate limited";
        case API_ERROR_SERVER: return "server error";
        default: return "unknown";
    }
}
