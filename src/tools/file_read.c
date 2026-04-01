#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *tool_execute_read_file(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *path = json_get_string(json, "file_path");
    int offset = json_get_int(json, "offset", 0);
    int limit = json_get_int(json, "limit", 0);
    cJSON_Delete(json);

    if (!path) return strdup("Error: 'file_path' argument required");

    FILE *f = fopen(path, "r");
    if (!f) {
        char err[256];
        snprintf(err, sizeof(err), "Error: cannot open file '%s'", path);
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
        return strbuf_detach(&listing);
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
    return strbuf_detach(&content);
}
