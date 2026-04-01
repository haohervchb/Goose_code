#ifndef SESSION_H
#define SESSION_H

#include "util/cJSON.h"

#define SESSION_VERSION 1

typedef struct {
    char *id;
    cJSON *messages;
    long total_input_tokens;
    long total_output_tokens;
    int turn_count;
} Session;

Session *session_new(void);
Session *session_load(const char *session_dir, const char *id);
int session_save(const char *session_dir, Session *sess);
void session_free(Session *sess);
void session_add_message(Session *sess, cJSON *msg);
void session_add_tool_result(Session *sess, const char *tool_call_id, const char *result);
int session_needs_compact(Session *sess, int context_window);
char *session_compact(Session *sess, int keep_recent);
char *session_list(const char *session_dir);

#endif
