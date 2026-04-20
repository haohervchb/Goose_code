#include "session.h"
#include "compact.h"
#include "tool_result_store.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#define SUMMARY_MAX_LEN 60

static char *session_path(const char *dir, const char *id) {
    size_t len = strlen(dir) + strlen(id) + 16;
    char *p = malloc(len);
    snprintf(p, len, "%s/%s.json", dir, id);
    return p;
}

static void generate_random_suffix(char *buf, size_t len) {
    static const char chars[] = "abcdef0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    buf[len - 1] = '\0';
}

Session *session_new(void) {
    Session *s = calloc(1, sizeof(*s));
    char ts[64];
    struct timespec ts_now;
    struct tm *tm_info;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    tm_info = localtime(&ts_now.tv_sec);
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    char suffix[8];
    generate_random_suffix(suffix, sizeof(suffix));
    
    size_t id_len = strlen(ts) + strlen(suffix) + 2;
    s->id = malloc(id_len);
    snprintf(s->id, id_len, "%s_%s", ts, suffix);
    
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
    const char *summary = json_get_string(json, "summary");
    if (summary) s->summary = strdup(summary);
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
    if (sess->summary) {
        cJSON_AddStringToObject(json, "summary", sess->summary);
    }
    int rc = json_write_file(path, json);
    cJSON_Delete(json);
    free(path);
    return rc;
}

void session_free(Session *sess) {
    if (!sess) return;
    free(sess->id);
    free(sess->summary);
    if (sess->messages) cJSON_Delete(sess->messages);
    free(sess->plan_content);
    free(sess);
}

void session_add_message(Session *sess, cJSON *msg) {
    cJSON_AddItemToArray(sess->messages, cJSON_Duplicate(msg, 1));
    sess->turn_count++;
    
    // Auto-generate summary from first user message if not set
    if (!sess->summary && msg) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (role && content && strcmp(role->valuestring, "user") == 0 && content->valuestring) {
            // Use first user message as summary (truncated)
            session_set_summary(sess, content->valuestring);
        }
    }
}

void session_add_tool_result(Session *sess, const GooseConfig *cfg, const char *tool_call_id, const char *result) {
    char *prepared = tool_result_store_prepare(cfg, sess, tool_call_id, result);
    
    cJSON *tool_result = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_result, "type", "tool_result");
    cJSON_AddStringToObject(tool_result, "tool_call_id", tool_call_id);
    cJSON_AddStringToObject(tool_result, "content", prepared);
    
    cJSON *content = cJSON_CreateArray();
    cJSON_AddItemToArray(content, tool_result);
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddItemToObject(msg, "content", content);
    
    cJSON_AddItemToArray(sess->messages, msg);
    free(prepared);
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

void session_set_summary(Session *sess, const char *summary) {
    if (!sess) return;
    free(sess->summary);
    sess->summary = NULL;
    if (summary && summary[0]) {
        // Truncate long summaries
        size_t len = strlen(summary);
        if (len > SUMMARY_MAX_LEN) {
            char *truncated = malloc(SUMMARY_MAX_LEN + 1);
            strncpy(truncated, summary, SUMMARY_MAX_LEN - 3);
            truncated[SUMMARY_MAX_LEN - 3] = '\0';
            strcat(truncated, "...");
            sess->summary = truncated;
        } else {
            sess->summary = strdup(summary);
        }
    }
}

const char *session_get_summary(const Session *sess) {
    if (!sess) return NULL;
    return sess->summary;
}

cJSON *session_normalize_for_api(const cJSON *messages) {
    if (!messages) return cJSON_CreateArray();
    
    cJSON *result = cJSON_CreateArray();
    int result_size = 0;
    
    cJSON *item;
    cJSON_ArrayForEach(item, messages) {
        cJSON *role = cJSON_GetObjectItem(item, "role");
        if (!role) continue;
        
        if (strcmp(role->valuestring, "user") == 0) {
            cJSON *content = cJSON_GetObjectItem(item, "content");
            
            if (cJSON_IsString(content)) {
                cJSON_AddItemToArray(result, cJSON_Duplicate(item, 1));
                result_size++;
            } else if (cJSON_IsArray(content)) {
                cJSON *user_text = NULL;
                cJSON *tool_results = cJSON_CreateArray();
                
                cJSON *block;
                cJSON_ArrayForEach(block, content) {
                    cJSON *type = cJSON_GetObjectItem(block, "type");
                    if (type && strcmp(type->valuestring, "tool_result") == 0) {
                        cJSON_AddItemToArray(tool_results, cJSON_Duplicate(block, 1));
                    } else if (!user_text) {
                        user_text = cJSON_Duplicate(block, 1);
                    }
                }
                
                if (user_text) {
                    cJSON *umsg = cJSON_CreateObject();
                    cJSON_AddStringToObject(umsg, "role", "user");
                    
                    cJSON *text_val = cJSON_GetObjectItem(user_text, "text");
                    if (text_val) {
                        cJSON_AddStringToObject(umsg, "content", text_val->valuestring);
                    } else {
                        cJSON_AddItemToObject(umsg, "content", user_text);
                    }
                    cJSON_AddItemToArray(result, umsg);
                    result_size++;
                }
                
                cJSON_Delete(tool_results);
            } else {
                cJSON_AddItemToArray(result, cJSON_Duplicate(item, 1));
                result_size++;
            }
        } else if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON_AddItemToArray(result, cJSON_Duplicate(item, 1));
            result_size++;
            
            cJSON *tool_calls = cJSON_GetObjectItem(item, "tool_calls");
            if (tool_calls && cJSON_IsArray(tool_calls)) {
                cJSON *tc;
                cJSON_ArrayForEach(tc, tool_calls) {
                    cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
                    if (!tc_id) continue;
                    
                    int found_result = 0;
                    int search_from = result_size;
                    int total = cJSON_GetArraySize(messages);
                    
                    for (int i = search_from; i < total && !found_result; i++) {
                        cJSON *next_msg = cJSON_GetArrayItem(messages, i);
                        cJSON *next_role = cJSON_GetObjectItem(next_msg, "role");
                        if (!next_role) continue;
                        
                        if (strcmp(next_role->valuestring, "user") == 0) {
                            cJSON *next_content = cJSON_GetObjectItem(next_msg, "content");
                            if (cJSON_IsArray(next_content)) {
                                cJSON *block;
                                cJSON_ArrayForEach(block, next_content) {
                                    cJSON *type = cJSON_GetObjectItem(block, "type");
                                    if (type && strcmp(type->valuestring, "tool_result") == 0) {
                                        cJSON *result_id = cJSON_GetObjectItem(block, "tool_call_id");
                                        if (result_id && strcmp(result_id->valuestring, tc_id->valuestring) == 0) {
                                            cJSON *tmsg = cJSON_CreateObject();
                                            cJSON_AddStringToObject(tmsg, "role", "tool");
                                            cJSON_AddStringToObject(tmsg, "tool_call_id", tc_id->valuestring);
                                            
                                            cJSON *res_content = cJSON_GetObjectItem(block, "content");
                                            if (cJSON_IsString(res_content)) {
                                                cJSON_AddStringToObject(tmsg, "content", res_content->valuestring);
                                            } else if (cJSON_IsArray(res_content)) {
                                                cJSON *txt = NULL;
                                                cJSON *b;
                                                cJSON_ArrayForEach(b, res_content) {
                                                    cJSON *bt = cJSON_GetObjectItem(b, "type");
                                                    if (bt && strcmp(bt->valuestring, "text") == 0) {
                                                        txt = cJSON_GetObjectItem(b, "text");
                                                        break;
                                                    }
                                                }
                                                cJSON_AddStringToObject(tmsg, "content", txt ? txt->valuestring : "");
                                            } else {
                                                cJSON_AddStringToObject(tmsg, "content", "");
                                            }
                                            
                                            cJSON_AddItemToArray(result, tmsg);
                                            result_size++;
                                            found_result = 1;
                                            break;
                                        }
                                    }
                                }
                            }
                        } else if (strcmp(next_role->valuestring, "tool") == 0) {
                            cJSON *result_id = cJSON_GetObjectItem(next_msg, "tool_call_id");
                            if (result_id && strcmp(result_id->valuestring, tc_id->valuestring) == 0) {
                                cJSON_AddItemToArray(result, cJSON_Duplicate(next_msg, 1));
                                result_size++;
                                found_result = 1;
                            }
                        }
                    }
                }
            }
        } else if (strcmp(role->valuestring, "tool") == 0) {
            cJSON *tool_call_id = cJSON_GetObjectItem(item, "tool_call_id");
            cJSON *tool_content = cJSON_GetObjectItem(item, "content");
            
            if (tool_call_id && tool_content) {
                cJSON_AddItemToArray(result, cJSON_Duplicate(item, 1));
                result_size++;
            }
        } else {
            cJSON_AddItemToArray(result, cJSON_Duplicate(item, 1));
            result_size++;
        }
    }
    return result;
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

    char *summary_msg = compact_build_user_summary_message(summary ? summary : "[Conversation context compacted]", keep_recent > 0);
    cJSON *compact_msg = json_build_message("user", summary_msg);
    free(summary_msg);
    cJSON_ReplaceItemInArray(sess->messages, 0, compact_msg);

    while (cJSON_GetArraySize(sess->messages) > keep_recent + 1) {
        cJSON_DeleteItemFromArray(sess->messages, 1);
    }
}

void session_record_compact_failure(Session *sess) {
    if (!sess) return;
    sess->compact_failure_count++;
    sess->compact_success_count = 0;
}

void session_record_compact_success(Session *sess) {
    if (!sess) return;
    sess->compact_success_count++;
    if (sess->compact_success_count >= 3) {
        sess->compact_failure_count = 0;
        sess->compact_success_count = 0;
    }
}

int session_compact_circuit_open(const Session *sess) {
    if (!sess) return 0;
    return sess->compact_failure_count >= MAX_CONSECUTIVE_COMPACT_FAILURES;
}

static int parse_session_id_datetime(const char *id, struct tm *tm_out) {
    if (!id || !tm_out) return -1;
    
    // Try new format: YYYYMMDD_HHMMSS_XXXXXX
    if (strlen(id) >= 15 && id[8] == '_') {
        int y, m, d, h, min, s;
        if (sscanf(id, "%4d%2d%2d_%2d%2d%2d", &y, &m, &d, &h, &min, &s) == 6) {
            tm_out->tm_year = y - 1900;
            tm_out->tm_mon = m - 1;
            tm_out->tm_mday = d;
            tm_out->tm_hour = h;
            tm_out->tm_min = min;
            tm_out->tm_sec = s;
            return 0;
        }
    }
    
    // Try old format: timestamp_nanoseconds
    if (strlen(id) > 10) {
        long ts = strtol(id, NULL, 10);
        if (ts > 0) {
            time_t t = ts;
            struct tm *tm = localtime(&t);
            if (tm) {
                *tm_out = *tm;
                return 0;
            }
        }
    }
    
    return -1;
}

typedef struct {
    char *id;
    time_t timestamp;
} SessionInfo;

static int session_info_cmp_desc(const void *a, const void *b) {
    const SessionInfo *sa = (const SessionInfo *)a;
    const SessionInfo *sb = (const SessionInfo *)b;
    // Descending order: latest first
    if (sa->timestamp > sb->timestamp) return -1;
    if (sa->timestamp < sb->timestamp) return 1;
    return 0;
}

char *session_list(const char *session_dir) {
    DIR *dir = opendir(session_dir);
    if (!dir) return strdup("No sessions found.");

    // First pass: collect all session IDs and their timestamps
    SessionInfo *sessions = NULL;
    int count = 0;
    int capacity = 0;
    struct dirent *ent;
    
    while ((ent = readdir(dir)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 5 && strcmp(ent->d_name + nlen - 5, ".json") == 0) {
            char id[256];
            memcpy(id, ent->d_name, nlen - 5);
            id[nlen - 5] = '\0';

            Session *s = session_load(session_dir, id);
            if (s) {
                // Expand array if needed
                if (count >= capacity) {
                    capacity = capacity == 0 ? 16 : capacity * 2;
                    sessions = realloc(sessions, capacity * sizeof(SessionInfo));
                }
                
                sessions[count].id = strdup(id);
                sessions[count].timestamp = 0;
                
                struct tm tm_info;
                if (parse_session_id_datetime(id, &tm_info) == 0) {
                    sessions[count].timestamp = mktime(&tm_info);
                }
                
                session_free(s);
                count++;
            }
        }
    }
    closedir(dir);
    
    // Sort by timestamp in descending order (latest first)
    if (count > 0) {
        qsort(sessions, count, sizeof(SessionInfo), session_info_cmp_desc);
    }
    
    // Second pass: display sessions in sorted order (top 10 only)
    StrBuf out = strbuf_from("Saved sessions:\n");
    int display_count = count < 10 ? count : 10;
    
    for (int i = 0; i < display_count; i++) {
        char datetime[64] = "unknown";
        if (sessions[i].timestamp > 0) {
            struct tm *tm = localtime(&sessions[i].timestamp);
            if (tm) {
                strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M", tm);
            }
        }
        
        Session *s = session_load(session_dir, sessions[i].id);
        if (s) {
            const char *summary = s->summary ? s->summary : "(no summary)";
            strbuf_append_fmt(&out, "  %-16s  %-16s  %-25s  %d msgs\n",
                              sessions[i].id, datetime, summary, cJSON_GetArraySize(s->messages));
            session_free(s);
        }
        
        free(sessions[i].id);
    }
    
    // Inform user about older sessions if there are more than 10
    if (count > 10) {
        strbuf_append_fmt(&out, "\n  ... and %d older session(s).\n", count - 10);
        strbuf_append_fmt(&out, "  Use 'ls -lt %s' to list all sessions sorted by time.\n", session_dir);
    }
    
    // Add instruction on how to resume a session
    strbuf_append_fmt(&out, "\n  To resume a session: use --session <id> on command line, or /session <id> in TUI.\n");
    
    if (count == 0) strbuf_append(&out, "  (none)\n");
    
    free(sessions);
    return strbuf_detach(&out);
}
