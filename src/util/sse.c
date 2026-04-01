#include "util/sse.h"
#include "util/json_util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void sse_parser_init(SseParser *p) {
    memset(p, 0, sizeof(*p));
}

void sse_parser_free(SseParser *p) {
    free(p->pending_args);
    p->pending_args = NULL;
    p->pending_args_cap = 0;
    p->pending_args_len = 0;
    p->pending_tool_name[0] = '\0';
    p->data_len = 0;
}

static void trim_trailing(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' ')) {
        s[--len] = '\0';
    }
}

SseEvent sse_parse_line(SseParser *p, const char *line, size_t len) {
    SseEvent ev = {0};

    if (len == 0 || (len == 1 && line[0] == '\n')) {
        if (p->data_len > 0) {
            p->data_buf[p->data_len] = '\0';
            if (strcmp(p->data_buf, "[DONE]") == 0) {
                ev.type = SSE_EVENT_DONE;
            } else {
                cJSON *json = cJSON_Parse(p->data_buf);
                if (json) {
                    cJSON *error = cJSON_GetObjectItem(json, "error");
                    if (error && cJSON_IsString(error)) {
                        ev.type = SSE_EVENT_ERROR;
                        ev.error = strdup(error->valuestring);
                    } else {
                        cJSON *choices = cJSON_GetObjectItem(json, "choices");
                        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                            cJSON *choice = cJSON_GetArrayItem(choices, 0);
                            cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                            cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");

                            if (delta && cJSON_IsObject(delta)) {
                                cJSON *content = cJSON_GetObjectItem(delta, "content");
                                if (content && cJSON_IsString(content) && content->valuestring[0]) {
                                    ev.type = SSE_EVENT_TEXT;
                                    ev.text = strdup(content->valuestring);
                                }

                                cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                                if (tool_calls && cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
                                    cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);
                                    cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
                                    cJSON *tc_index = cJSON_GetObjectItem(tc, "index");
                                    cJSON *fn = cJSON_GetObjectItem(tc, "function");
                                    
                                    int has_name = 0;
                                    char *new_args = NULL;
                                    
                                    if (fn) {
                                        cJSON *fname = cJSON_GetObjectItem(fn, "name");
                                        cJSON *fargs = cJSON_GetObjectItem(fn, "arguments");
                                        
                                        if (fname && fname->valuestring) {
                                            has_name = 1;
                                            if (tc_id && tc_id->valuestring) {
                                                strncpy(p->pending_tool_id, tc_id->valuestring, sizeof(p->pending_tool_id) - 1);
                                                strncpy(p->pending_tool_name, fname->valuestring, sizeof(p->pending_tool_name) - 1);
                                                p->pending_tool_idx = tc_index ? tc_index->valueint : 0;
                                            }
                                            if (fargs && fargs->valuestring && strlen(fargs->valuestring) > 0) {
                                                new_args = fargs->valuestring;
                                            }
                                        } else if (fargs && fargs->valuestring && strlen(fargs->valuestring) > 0) {
                                            new_args = fargs->valuestring;
                                        }
                                    }
                                    
                                    if ((p->pending_tool_name[0] || has_name) && new_args) {
                                        size_t new_len = strlen(new_args);
                                        if (p->pending_args_len + new_len + 1 > p->pending_args_cap) {
                                            p->pending_args_cap = (p->pending_args_len + new_len + 1) * 2;
                                            p->pending_args = realloc(p->pending_args, p->pending_args_cap);
                                        }
                                        memcpy(p->pending_args + p->pending_args_len, new_args, new_len);
                                        p->pending_args_len += new_len;
                                        p->pending_args[p->pending_args_len] = '\0';
                                    }
                                }
                            }
                            
                            if (finish && cJSON_IsString(finish) &&
                                (strcmp(finish->valuestring, "stop") == 0 ||
                                 strcmp(finish->valuestring, "tool_calls") == 0)) {
                                if (strcmp(finish->valuestring, "stop") == 0) {
                                    ev.type = SSE_EVENT_DONE;
                                    ev.finish_reason_stop = 1;
                                } else {
                                    ev.finish_reason_tool_calls = 1;
                                }
                                if (p->pending_tool_name[0] && p->pending_args_len > 0) {
                                    ev.type = SSE_EVENT_TOOL_CALL;
                                    ev.tool_call_id = strdup(p->pending_tool_id);
                                    ev.tool_name = strdup(p->pending_tool_name);
                                    ev.tool_args = strdup(p->pending_args);
                                    p->pending_tool_name[0] = '\0';
                                    p->pending_args_len = 0;
                                }
                            }
                        }
                    }
                    cJSON_Delete(json);
                }
            }
            p->data_len = 0;
        }
        memset(p->event_type, 0, sizeof(p->event_type));
        return ev;
    }

    if (strncmp(line, "data: ", 6) == 0) {
        const char *d = line + 6;
        size_t dlen = len - 6;
        if (p->data_len + dlen < sizeof(p->data_buf) - 1) {
            memcpy(p->data_buf + p->data_len, d, dlen);
            p->data_len += dlen;
        }
    } else if (strncmp(line, "event: ", 7) == 0) {
        strncpy(p->event_type, line + 7, sizeof(p->event_type) - 1);
        trim_trailing(p->event_type);
    }

    memset(&ev, 0, sizeof(ev));
    return ev;
}

void sse_event_free(SseEvent *e) {
    free(e->text);
    free(e->tool_call_id);
    free(e->tool_name);
    free(e->tool_args);
    free(e->error);
}
