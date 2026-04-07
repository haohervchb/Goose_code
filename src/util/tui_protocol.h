#ifndef TUI_PROTOCOL_H
#define TUI_PROTOCOL_H

#include <stddef.h>

typedef enum {
    TUI_MSG_INVALID,
    TUI_MSG_INIT,
    TUI_MSG_PROMPT,
    TUI_MSG_COMMAND,
    TUI_MSG_QUIT,
    TUI_MSG_PING
} TUIMessageType;

typedef struct {
    TUIMessageType type;
    char *working_dir;
    char *model;
    char *provider;
    char *base_url;
    char *text;      // For prompt
    char *cmd_name;  // For command
    char *cmd_args;  // For command
} TUIRequest;

void tui_protocol_init(void);
void tui_protocol_cleanup(void);
int tui_protocol_read_request(TUIRequest *req);
void tui_protocol_free_request(TUIRequest *req);

void tui_protocol_send_init_ok(const char *session_id, const char *session_dir);
void tui_protocol_send_response_chunk(const char *content, int done);
void tui_protocol_send_tool_start(const char *name, const char *id, const char *args_json);
void tui_protocol_send_tool_output(const char *id, const char *output);
void tui_protocol_send_tool_end(const char *id, int success, const char *error);
void tui_protocol_send_error(const char *message);
void tui_protocol_send_session_info(int message_count, int plan_mode);

#endif // TUI_PROTOCOL_H