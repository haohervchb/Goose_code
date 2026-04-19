#include "util/sse.h"
#include "util/json_util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void sse_parser_init(SseParser *p) {
    memset(p, 0, sizeof(*p));
}

static void pending_tool_reset(SsePendingToolCall *tool) {
    free(tool->args);
    memset(tool, 0, sizeof(*tool));
}

static void queued_event_reset(SseEvent *ev) {
    memset(ev, 0, sizeof(*ev));
}

static SsePendingToolCall *pending_tool_get(SseParser *p, int index) {
    for (int i = 0; i < SSE_MAX_TOOL_CALLS; i++) {
        if (p->pending_tools[i].used && p->pending_tools[i].index == index) {
            return &p->pending_tools[i];
        }
    }

    for (int i = 0; i < SSE_MAX_TOOL_CALLS; i++) {
        if (!p->pending_tools[i].used) {
            p->pending_tools[i].used = 1;
            p->pending_tools[i].index = index;
            return &p->pending_tools[i];
        }
    }

    return NULL;
}

static void pending_tool_append_args(SsePendingToolCall *tool, const char *args) {
    size_t args_len = strlen(args);

    if (tool->args_len + args_len + 1 > tool->args_cap) {
        tool->args_cap = (tool->args_len + args_len + 1) * 2;
        tool->args = realloc(tool->args, tool->args_cap);
    }

    memcpy(tool->args + tool->args_len, args, args_len);
    tool->args_len += args_len;
    tool->args[tool->args_len] = '\0';
}

static void queue_pending_tool_event(SseParser *p, SsePendingToolCall *tool,
                                     int finish_reason_tool_calls) {
    if (!tool->used || !tool->name[0]) return;
    if (p->queued_event_count >= SSE_MAX_TOOL_CALLS) return;

    SseEvent *ev = &p->queued_events[p->queued_event_count++];
    queued_event_reset(ev);
    ev->type = SSE_EVENT_TOOL_CALL;
    ev->tool_call_id = strdup(tool->id);
    ev->tool_name = strdup(tool->name);
    ev->tool_args = tool->args_len > 0 ? strdup(tool->args) : strdup("{}");
    ev->finish_reason_tool_calls = finish_reason_tool_calls;

    pending_tool_reset(tool);
}

static void queue_all_pending_tool_events(SseParser *p, int finish_reason_tool_calls) {
    for (int i = 0; i < SSE_MAX_TOOL_CALLS; i++) {
        queue_pending_tool_event(p, &p->pending_tools[i], finish_reason_tool_calls);
    }
}

void sse_parser_free(SseParser *p) {
    for (int i = 0; i < SSE_MAX_TOOL_CALLS; i++) {
        pending_tool_reset(&p->pending_tools[i]);
    }

    for (int i = p->queued_event_index; i < p->queued_event_count; i++) {
        sse_event_free(&p->queued_events[i]);
        queued_event_reset(&p->queued_events[i]);
    }

    p->queued_event_count = 0;
    p->queued_event_index = 0;
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
    int has_primary_event = 0;

    if (len == 0 || (len == 1 && line[0] == '\n')) {
        if (p->data_len > 0) {
            p->data_buf[p->data_len] = '\0';
            if (strcmp(p->data_buf, "[DONE]") == 0) {
                ev.type = SSE_EVENT_DONE;
                has_primary_event = 1;
            } else {
cJSON *json = cJSON_Parse(p->data_buf);
                if (json) {
                    // Parse usage
                    cJSON *usage = cJSON_GetObjectItem(json, "usage");
                    if (usage) {
                        p->usage_input_tokens = json_get_int(usage, "prompt_tokens", 0);
                        p->usage_output_tokens = json_get_int(usage, "completion_tokens", 0);
                        p->usage_cache_read_tokens = json_get_int(usage, "cache_read_input_tokens", 0);
                        p->usage_cache_creation_tokens = json_get_int(usage, "cache_creation_input_tokens", 0);
                    }
                    cJSON *error = cJSON_GetObjectItem(json, "error");
                    if (error && cJSON_IsString(error)) {
                        ev.type = SSE_EVENT_ERROR;
                        ev.error = strdup(error->valuestring);
                        has_primary_event = 1;
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
                                    has_primary_event = 1;
                                }

                                cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                                if (tool_calls && cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
                                    int tool_call_count = cJSON_GetArraySize(tool_calls);
                                    for (int i = 0; i < tool_call_count; i++) {
                                        cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                                        if (!tc || !cJSON_IsObject(tc)) continue;

                                        cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
                                        cJSON *tc_index = cJSON_GetObjectItem(tc, "index");
                                        cJSON *fn = cJSON_GetObjectItem(tc, "function");
                                        int index = (tc_index && cJSON_IsNumber(tc_index)) ? tc_index->valueint : i;
                                        SsePendingToolCall *tool = pending_tool_get(p, index);

                                        if (!tool) continue;
                                        if (tc_id && cJSON_IsString(tc_id) && tc_id->valuestring) {
                                            strncpy(tool->id, tc_id->valuestring, sizeof(tool->id) - 1);
                                        }

                                        if (fn && cJSON_IsObject(fn)) {
                                            cJSON *fname = cJSON_GetObjectItem(fn, "name");
                                            cJSON *fargs = cJSON_GetObjectItem(fn, "arguments");

                                            if (fname && cJSON_IsString(fname) && fname->valuestring) {
                                                strncpy(tool->name, fname->valuestring, sizeof(tool->name) - 1);
                                            }

                                            if (fargs && cJSON_IsString(fargs) && fargs->valuestring[0]) {
                                                pending_tool_append_args(tool, fargs->valuestring);
                                            }
                                        }
                                    }
                                }
                            }
                            
                            if (finish && cJSON_IsString(finish) &&
                                (strcmp(finish->valuestring, "stop") == 0 ||
                                 strcmp(finish->valuestring, "tool_calls") == 0)) {
                                if (strcmp(finish->valuestring, "stop") == 0) {
                                    ev.finish_reason_stop = 1;
                                    if (!has_primary_event) {
                                        ev.type = SSE_EVENT_DONE;
                                        has_primary_event = 1;
                                    }
                                } else {
                                    ev.finish_reason_tool_calls = 1;
                                    queue_all_pending_tool_events(p, 1);
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

        if (!has_primary_event) {
            return sse_parser_next_event(p);
        }

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

SseEvent sse_parser_next_event(SseParser *p) {
    SseEvent ev = {0};

    if (p->queued_event_index >= p->queued_event_count) {
        p->queued_event_index = 0;
        p->queued_event_count = 0;
        return ev;
    }

    ev = p->queued_events[p->queued_event_index];
    queued_event_reset(&p->queued_events[p->queued_event_index]);
    p->queued_event_index++;

    if (p->queued_event_index >= p->queued_event_count) {
        p->queued_event_index = 0;
        p->queued_event_count = 0;
    }

    return ev;
}

void sse_event_free(SseEvent *e) {
    free(e->text);
    free(e->tool_call_id);
    free(e->tool_name);
    free(e->tool_args);
    free(e->error);
}

long sse_parser_usage_input_tokens(SseParser *p) {
    return p->usage_input_tokens;
}

long sse_parser_usage_output_tokens(SseParser *p) {
    return p->usage_output_tokens;
}
