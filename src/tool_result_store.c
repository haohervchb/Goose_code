#include "tool_result_store.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_TOOL_RESULT_BYTES 4000
#define TOOL_RESULT_PREVIEW_BYTES 2000
#define MAX_TOOL_RESULTS_PER_MESSAGE_BYTES 9000

static char *tool_result_dir_for_session(const GooseConfig *cfg, const Session *sess) {
    size_t len = strlen(cfg->tool_result_dir) + strlen(sess->id) + 2;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", cfg->tool_result_dir, sess->id);
    mkdir(path, 0755);
    return path;
}

static char *tool_result_path(const GooseConfig *cfg, const Session *sess, const char *tool_call_id) {
    char *dir = tool_result_dir_for_session(cfg, sess);
    size_t len = strlen(dir) + strlen(tool_call_id) + 8;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s.txt", dir, tool_call_id);
    free(dir);
    return path;
}

static void write_tool_result(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

static char *tool_result_store_persist_forced(const GooseConfig *cfg, const Session *sess,
                                              const char *tool_call_id, const char *content) {
    char *path = tool_result_path(cfg, sess, tool_call_id);
    write_tool_result(path, content);

    StrBuf out = strbuf_new();
    strbuf_append(&out, "<persisted-output>\n");
    strbuf_append_fmt(&out, "Output too large (%zu bytes). Full output saved to: %s\n\n", strlen(content), path);
    strbuf_append_fmt(&out, "Preview (first %d bytes):\n", TOOL_RESULT_PREVIEW_BYTES);
    strbuf_append_len(&out, content, TOOL_RESULT_PREVIEW_BYTES);
    if (strlen(content) > TOOL_RESULT_PREVIEW_BYTES) strbuf_append(&out, "\n...\n");
    strbuf_append(&out, "</persisted-output>");
    free(path);
    return strbuf_detach(&out);
}

char *tool_result_store_prepare(const GooseConfig *cfg, const Session *sess,
                                const char *tool_call_id, const char *content) {
    if (!content) return strdup("");

    size_t len = strlen(content);
    if (len <= MAX_TOOL_RESULT_BYTES) return strdup(content);

    return tool_result_store_persist_forced(cfg, sess, tool_call_id, content);
}

char **tool_result_store_prepare_batch(const GooseConfig *cfg, const Session *sess,
                                       const char **tool_call_ids, char **contents, int count) {
    char **prepared = calloc((size_t)count, sizeof(char *));
    int *persisted = calloc((size_t)count, sizeof(int));
    size_t total = 0;

    for (int i = 0; i < count; i++) {
        prepared[i] = tool_result_store_prepare(cfg, sess, tool_call_ids[i], contents[i]);
        if (prepared[i] && strstr(prepared[i], "<persisted-output>") != NULL) persisted[i] = 1;
        if (prepared[i]) total += strlen(prepared[i]);
    }

    while (total > MAX_TOOL_RESULTS_PER_MESSAGE_BYTES) {
        int idx = -1;
        size_t max_len = 0;
        for (int i = 0; i < count; i++) {
            if (persisted[i] || !contents[i]) continue;
            size_t len = strlen(contents[i]);
            if (len > max_len) {
                max_len = len;
                idx = i;
            }
        }
        if (idx < 0) break;

        total -= prepared[idx] ? strlen(prepared[idx]) : 0;
        free(prepared[idx]);
        prepared[idx] = tool_result_store_persist_forced(cfg, sess, tool_call_ids[idx], contents[idx]);
        persisted[idx] = 1;
        total += prepared[idx] ? strlen(prepared[idx]) : 0;
    }

    free(persisted);
    return prepared;
}
