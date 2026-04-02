#include "tool_result_store.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_TOOL_RESULT_BYTES 4000
#define TOOL_RESULT_PREVIEW_BYTES 2000

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

static void write_if_missing(const char *path, const char *content) {
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

char *tool_result_store_prepare(const GooseConfig *cfg, const Session *sess,
                                const char *tool_call_id, const char *content) {
    if (!content) return strdup("");

    size_t len = strlen(content);
    if (len <= MAX_TOOL_RESULT_BYTES) return strdup(content);

    char *path = tool_result_path(cfg, sess, tool_call_id);
    write_if_missing(path, content);

    StrBuf out = strbuf_new();
    strbuf_append(&out, "<persisted-output>\n");
    strbuf_append_fmt(&out, "Output too large (%zu bytes). Full output saved to: %s\n\n", len, path);
    strbuf_append_fmt(&out, "Preview (first %d bytes):\n", TOOL_RESULT_PREVIEW_BYTES);
    strbuf_append_len(&out, content, TOOL_RESULT_PREVIEW_BYTES);
    if (len > TOOL_RESULT_PREVIEW_BYTES) strbuf_append(&out, "\n...\n");
    strbuf_append(&out, "</persisted-output>");
    free(path);
    return strbuf_detach(&out);
}
