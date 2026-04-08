#ifndef SESSION_H
#define SESSION_H

#include "config.h"
#include "util/cJSON.h"

#define SESSION_VERSION 1
#define MAX_CONSECUTIVE_COMPACT_FAILURES 3

typedef struct {
    char *id;
    char *summary;
    cJSON *messages;
    long total_input_tokens;
    long total_output_tokens;
    int turn_count;
    int plan_mode;
    char *plan_content;
    int compact_failure_count;
    int compact_success_count;
} Session;

Session *session_new(void);
Session *session_load(const char *session_dir, const char *id);
int session_save(const char *session_dir, Session *sess);
void session_free(Session *sess);
void session_add_message(Session *sess, cJSON *msg);
void session_add_tool_result(Session *sess, const GooseConfig *cfg, const char *tool_call_id, const char *result);
void session_set_plan_mode(Session *sess, int enabled);
void session_set_plan(Session *sess, const char *plan);
void session_clear_plan(Session *sess);
const char *session_get_plan(const Session *sess);
int session_needs_compact(Session *sess, int context_window);
char *session_compact(Session *sess, int keep_recent);
void session_apply_compact_summary(Session *sess, int keep_recent, const char *summary);
void session_record_compact_failure(Session *sess);
void session_record_compact_success(Session *sess);
int session_compact_circuit_open(const Session *sess);
char *session_list(const char *session_dir);
void session_set_summary(Session *sess, const char *summary);
const char *session_get_summary(const Session *sess);
cJSON *session_normalize_for_api(const cJSON *messages);

#endif
