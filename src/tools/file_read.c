#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

static int path_is_within_dir(const char *path, const char *dir) {
    char resolved_path[PATH_MAX];
    char resolved_dir[PATH_MAX];

    if (!realpath(dir, resolved_dir)) return 0;

    if (!realpath(path, resolved_path)) {
        char *dir_copy = strdup(path);
        if (!dir_copy) return 0;
        char *parent = dirname(dir_copy);
        int parent_ok = 0;
        if (realpath(parent, resolved_dir)) {
            char *base = basename(dir_copy);
            size_t dir_len = strlen(resolved_dir);
            size_t base_len = strlen(base);
            if (dir_len + 1 + base_len < PATH_MAX) {
                snprintf(resolved_path, PATH_MAX, "%s/%s", resolved_dir, base);
                parent_ok = 1;
            }
        }
        free(dir_copy);
        if (!parent_ok) return 0;
    }

    size_t dir_len = strlen(resolved_dir);
    if (strncmp(resolved_path, resolved_dir, dir_len) != 0) return 0;
    if (resolved_path[dir_len] != '\0' && resolved_path[dir_len] != '/') return 0;
    return 1;
}

char *tool_execute_read_file(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *path = json_get_string(json, "file_path");
    int offset = json_get_int(json, "offset", 0);
    int limit = json_get_int(json, "limit", 0);

    if (!path) {
        cJSON_Delete(json);
        return strdup("Error: 'file_path' argument required");
    }

    if (cfg && cfg->working_dir && cfg->working_dir[0]) {
        if (!path_is_within_dir(path, cfg->working_dir)) {
            char err[512];
            snprintf(err, sizeof(err), "Error: file '%s' is outside the working directory '%s'", path, cfg->working_dir);
            cJSON_Delete(json);
            return strdup(err);
        }
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        char err[256];
        snprintf(err, sizeof(err), "Error: cannot open file '%s'", path);
        cJSON_Delete(json);
        return strdup(err);
    }

    struct stat st;
    fstat(fileno(f), &st);
    if (S_ISDIR(st.st_mode)) {
        StrBuf listing = strbuf_from("Directory listing:\n");
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "ls -la \"%s\" 2>/dev/null", path);
        FILE *ls = popen(cmd, "r");
        if (ls) {
            char buf[256];
            while (fgets(buf, sizeof(buf), ls)) strbuf_append(&listing, buf);
            pclose(ls);
        }
        fclose(f);
        char *result = strbuf_detach(&listing);
        cJSON_Delete(json);
        return result;
    }

    StrBuf content = strbuf_new();
    char buf[4096];
    int line = 0;
    while (fgets(buf, sizeof(buf), f)) {
        line++;
        if (offset > 0 && line < offset) continue;
        if (limit > 0 && line >= offset + limit) break;
        strbuf_append_fmt(&content, "%4d: %s", line, buf);
    }
    fclose(f);

    if (content.len > 500000) {
        content.data[500000] = '\0';
        content.len = 500000;
        strbuf_append(&content, "\n\n[File truncated at 500KB]");
    }
    char *result = strbuf_detach(&content);
    cJSON_Delete(json);
    return result;
}
