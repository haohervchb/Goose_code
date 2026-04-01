#ifndef SSE_H
#define SSE_H

#include <stddef.h>

typedef enum {
    SSE_EVENT_TEXT,
    SSE_EVENT_TOOL_CALL,
    SSE_EVENT_DONE,
    SSE_EVENT_ERROR
} SseEventType;

typedef struct {
    SseEventType type;
    char *text;
    char *tool_call_id;
    char *tool_name;
    char *tool_args;
    char *error;
    int finish_reason_stop;
    int finish_reason_tool_calls;
} SseEvent;

#define SSE_MAX_TOOL_CALLS 16

typedef struct {
    int used;
    int index;
    char id[128];
    char name[128];
    char *args;
    size_t args_len;
    size_t args_cap;
} SsePendingToolCall;

typedef struct {
    char line[8192];
    size_t line_len;
    char event_type[64];
    char data_buf[65536];
    size_t data_len;
    SsePendingToolCall pending_tools[SSE_MAX_TOOL_CALLS];
    SseEvent queued_events[SSE_MAX_TOOL_CALLS];
    int queued_event_count;
    int queued_event_index;
} SseParser;

void sse_parser_init(SseParser *p);
void sse_parser_free(SseParser *p);
SseEvent sse_parse_line(SseParser *p, const char *line, size_t len);
SseEvent sse_parser_next_event(SseParser *p);
void sse_event_free(SseEvent *e);

#endif
