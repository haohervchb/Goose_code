#include "session.h"
#include "compact.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_TOOL_RESULT_BYTES 4000

static char *truncate_tool_result_for_session(const char *result) {
    if (!result) return strdup("");

    size_t len = strlen(result);
    if (len <= MAX_TOOL_RESULT_BYTES) return strdup(result);

    const size_t head = 2600;
    const size_t tail = 900;
    StrBuf out = strbuf_new();
    strbuf_append_len(&out, result, head);
    strbuf_append_fmt(&out, "\n\n[Tool result truncated from %zu bytes to keep the request body manageable]\n\n", len);
    strbuf_append(&out, result + (len - tail));
    return strbuf_detach(&out);
}

static char *session_path(const char *dir, const char *id) {
    size_t len = strlen(dir) + strlen(id) + 16;
    char *p = malloc(len);
    snprintf(p, len, "%s/%s.json", dir, id);
    return p;
}

Session *session_new(void) {
    Session *s = calloc(1, sizeof(*s));
    char ts[64];
    struct timespec ts_now;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    snprintf(ts, sizeof(ts), "%ld_%ld", (long)ts_now.tv_sec, (long)ts_now.tv_nsec);
    s->id = strdup(ts);
    s->messages = cJSON_CreateArray();
    return s;
}

Session *session_load(const char *session_dir, const char *id) {
    char *path = session_path(session_dir, id);
    char *data = json_read_file(path);
    free(path);
    if (!data) return NULL;

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return NULL;

    Session *s = calloc(1, sizeof(*s));
    s->id = strdup(id);
    s->messages = cJSON_GetObjectItem(json, "messages");
    if (s->messages) s->messages = cJSON_Duplicate(s->messages, 1);
    else s->messages = cJSON_CreateArray();
    s->total_input_tokens = json_get_int(json, "input_tokens", 0);
    s->total_output_tokens = json_get_int(json, "output_tokens", 0);
    s->turn_count = json_get_int(json, "turn_count", 0);
    s->plan_mode = json_get_int(json, "plan_mode", 0);
    const char *plan_content = json_get_string(json, "plan_content");
    if (plan_content) s->plan_content = strdup(plan_content);
    cJSON_Delete(json);
    return s;
}

int session_save(const char *session_dir, Session *sess) {
    char *path = session_path(session_dir, sess->id);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "version", SESSION_VERSION);
    cJSON_AddStringToObject(json, "id", sess->id);
    cJSON_AddItemToObject(json, "messages", cJSON_Duplicate(sess->messages, 1));
    cJSON_AddNumberToObject(json, "input_tokens", sess->total_input_tokens);
    cJSON_AddNumberToObject(json, "output_tokens", sess->total_output_tokens);
    cJSON_AddNumberToObject(json, "turn_count", sess->turn_count);
    cJSON_AddNumberToObject(json, "plan_mode", sess->plan_mode);
    if (sess->plan_content) {
        cJSON_AddStringToObject(json, "plan_content", sess->plan_content);
    }
    int rc = json_write_file(path, json);
    cJSON_Delete(json);
    free(path);
    return rc;
}

void session_free(Session *sess) {
    if (!sess) return;
    free(sess->id);
    if (sess->messages) cJSON_Delete(sess->messages);
    free(sess->plan_content);
    free(sess);
}

void session_add_message(Session *sess, cJSON *msg) {
    cJSON_AddItemToArray(sess->messages, cJSON_Duplicate(msg, 1));
    sess->turn_count++;
}

void session_add_tool_result(Session *sess, const char *tool_call_id, const char *result) {
    char *truncated = truncate_tool_result_for_session(result);
    cJSON *msg = json_build_tool_result(tool_call_id, truncated);
    free(truncated);
    cJSON_AddItemToArray(sess->messages, msg);
}

void session_set_plan_mode(Session *sess, int enabled) {
    if (!sess) return;
    sess->plan_mode = enabled ? 1 : 0;
}

void session_set_plan(Session *sess, const char *plan) {
    if (!sess) return;
    free(sess->plan_content);
    sess->plan_content = NULL;
    if (plan && plan[0]) {
        sess->plan_content = strdup(plan);
    }
}

void session_clear_plan(Session *sess) {
    if (!sess) return;
    free(sess->plan_content);
    sess->plan_content = NULL;
}

const char *session_get_plan(const Session *sess) {
    if (!sess) return NULL;
    return sess->plan_content;
}

int session_needs_compact(Session *sess, int context_window) {
    if (!sess || !sess->messages) return 0;
    int msg_count = cJSON_GetArraySize(sess->messages);
    return msg_count > (context_window / 1000);
}

char *session_compact(Session *sess, int keep_recent) {
    if (!sess || !sess->messages) return NULL;
    int total = cJSON_GetArraySize(sess->messages);
    if (total <= keep_recent + 1) return NULL;

    int compact_to = total - keep_recent;
    StrBuf summary = strbuf_from("[Previous conversation summarized: ");

    for (int i = 1; i < compact_to && i < total; i++) {
        cJSON *msg = cJSON_GetArrayItem(sess->messages, i);
        const char *role = json_get_string(msg, "role");
        const char *content = json_get_string(msg, "content");
        if (role && content) {
            if (strcmp(role, "user") == 0) {
                strbuf_append_fmt(&summary, "User: %.100s... ", content);
            } else if (strcmp(role, "assistant") == 0) {
                strbuf_append_fmt(&summary, "Assistant: %.100s... ", content);
            }
        }
    }
    strbuf_append(&summary, "]");

    session_apply_compact_summary(sess, keep_recent, summary.data);

    char *result = strbuf_detach(&summary);
    strbuf_free(&summary);
    return result;
}

void session_apply_compact_summary(Session *sess, int keep_recent, const char *summary) {
    if (!sess || !sess->messages) return;
    int total = cJSON_GetArraySize(sess->messages);
    if (total <= keep_recent + 1) return;

    int compact_to = total - keep_recent;
    char *summary_msg = compact_build_user_summary_message(summary ? summary : "[Conversation context compacted]", keep_recent > 0);
    cJSON *compact_msg = json_build_message("user", summary_msg);
    free(summary_msg);
    cJSON_ReplaceItemInArray(sess->messages, 0, compact_msg);

    for (int i = compact_to; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(sess->messages, i);
        if (item) {
            cJSON_DetachItemFromArray(sess->messages, i);
            cJSON_AddItemToArray(sess->messages, item);
        }
    }

    while (cJSON_GetArraySize(sess->messages) > keep_recent + 1) {
        cJSON_DeleteItemFromArray(sess->messages, 1);
    }
}

char *session_list(const char *session_dir) {
    DIR *dir = opendir(session_dir);
    if (!dir) return strdup("No sessions found.");

    StrBuf out = strbuf_from("Saved sessions:\n");
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 5 && strcmp(ent->d_name + nlen - 5, ".json") == 0) {
            char id[256];
            memcpy(id, ent->d_name, nlen - 5);
            id[nlen - 5] = '\0';

            Session *s = session_load(session_dir, id);
            if (s) {
                strbuf_append_fmt(&out, "  %-40s  %d msgs  in=%ld out=%ld\n",
                                  s->id, cJSON_GetArraySize(s->messages),
                                  s->total_input_tokens, s->total_output_tokens);
                session_free(s);
                count++;
            }
        }
    }
    closedir(dir);
    if (count == 0) strbuf_append(&out, "  (none)\n");
    return strbuf_detach(&out);
}
