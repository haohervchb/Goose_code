#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static int ensure_parent_dirs(const char *path) {
    char *dir = strdup(path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        free(dir);
        return 0;
    }

    *slash = '\0';
    for (char *p = dir + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0755);
            *p = '/';
        }
    }
    mkdir(dir, 0755);
    free(dir);
    return 0;
}

char *tool_execute_write_file(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *path = json_get_string(json, "file_path");
    const char *content = json_get_string(json, "content");

    if (!path) {
        cJSON_Delete(json);
        return strdup("Error: 'file_path' argument required");
    }
    if (!content) {
        cJSON_Delete(json);
        return strdup("Error: 'content' argument required");
    }

    ensure_parent_dirs(path);

    FILE *f = fopen(path, "w");
    if (!f) {
        char err[512];
        snprintf(err, sizeof(err), "Error: cannot write to '%s': %s", path, strerror(errno));
        cJSON_Delete(json);
        return strdup(err);
    }
    fputs(content, f);
    fclose(f);

    StrBuf result = strbuf_new();
    strbuf_append_fmt(&result, "Successfully wrote %zu bytes to %s", strlen(content), path);
    char *out = strbuf_detach(&result);
    cJSON_Delete(json);
    return out;
}
