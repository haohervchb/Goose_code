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

typedef struct {
    char line[8192];
    size_t line_len;
    char event_type[64];
    char data_buf[65536];
    size_t data_len;
    char pending_tool_id[128];
    char pending_tool_name[128];
    int pending_tool_idx;
    char *pending_args;
    size_t pending_args_len;
    size_t pending_args_cap;
} SseParser;

void sse_parser_init(SseParser *p);
void sse_parser_free(SseParser *p);
SseEvent sse_parse_line(SseParser *p, const char *line, size_t len);
void sse_event_free(SseEvent *e);

#endif
