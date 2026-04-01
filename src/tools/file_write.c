#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

char *tool_execute_write_file(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *path = json_get_string(json, "file_path");
    const char *content = json_get_string(json, "content");
    cJSON_Delete(json);

    if (!path) return strdup("Error: 'file_path' argument required");
    if (!content) return strdup("Error: 'content' argument required");

    char *dir = strdup(path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }
    free(dir);

    FILE *f = fopen(path, "w");
    if (!f) {
        char err[512];
        snprintf(err, sizeof(err), "Error: cannot write to '%s': %s", path, strerror(errno));
        return strdup(err);
    }
    fputs(content, f);
    fclose(f);

    StrBuf result = strbuf_new();
    strbuf_append_fmt(&result, "Successfully wrote %zu bytes to %s", strlen(content), path);
    return strbuf_detach(&result);
}
