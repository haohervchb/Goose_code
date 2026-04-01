#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_edit_file(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *path = json_get_string(json, "file_path");
    const char *old_str = json_get_string(json, "old_string");
    const char *new_str = json_get_string(json, "new_string");
    cJSON_Delete(json);

    if (!path || !old_str || !new_str)
        return strdup("Error: 'file_path', 'old_string', and 'new_string' arguments required");

    FILE *f = fopen(path, "r");
    if (!f) {
        char err[512];
        snprintf(err, sizeof(err), "Error: cannot open file '%s'", path);
        return strdup(err);
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *file_content = malloc(sz + 1);
    size_t rd = fread(file_content, 1, sz, f);
    file_content[rd] = '\0';
    fclose(f);

    char *pos = strstr(file_content, old_str);
    if (!pos) {
        free(file_content);
        char err[512];
        snprintf(err, sizeof(err), "Error: 'old_string' not found in %s", path);
        return strdup(err);
    }

    size_t before_len = (size_t)(pos - file_content);
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t new_sz = rd - old_len + new_len;
    char *new_content = malloc(new_sz + 1);
    memcpy(new_content, file_content, before_len);
    memcpy(new_content + before_len, new_str, new_len);
    memcpy(new_content + before_len + new_len, pos + old_len, rd - old_len - before_len);
    new_content[new_sz] = '\0';
    free(file_content);

    f = fopen(path, "w");
    if (!f) { free(new_content); return strdup("Error: cannot write file"); }
    fwrite(new_content, 1, new_sz, f);
    fclose(f);
    free(new_content);

    StrBuf result = strbuf_new();
    strbuf_append_fmt(&result, "Successfully edited %s (replaced %zu chars with %zu chars)", path, old_len, new_len);
    return strbuf_detach(&result);
}
