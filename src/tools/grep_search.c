#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *shell_quote_single_grep(const char *value) {
    size_t len = strlen(value);
    size_t extra = 2;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') extra += 3;
    }

    char *out = malloc(len + extra + 1);
    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = value[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

char *tool_execute_grep_search(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *pattern = json_get_string(json, "pattern");
    const char *path = json_get_string(json, "path");
    int case_sensitive = json_get_int(json, "case_sensitive", 0);
    const char *glob = json_get_string(json, "glob");
    int head_limit = json_get_int(json, "head_limit", 200);
    int context_before = json_get_int(json, "-B", 0);
    int context_after = json_get_int(json, "-A", 0);
    int context = json_get_int(json, "context", 0);
    const char *type = json_get_string(json, "type");
    const char *output_mode = json_get_string(json, "output_mode");

    if (!pattern) {
        cJSON_Delete(json);
        return strdup("Error: 'pattern' argument required");
    }

    char *quoted_pattern = shell_quote_single_grep(pattern);
    char *quoted_path = path ? shell_quote_single_grep(path) : NULL;
    char *quoted_glob = glob ? shell_quote_single_grep(glob) : NULL;
    char type_pat[64] = {0};
    char *quoted_type = NULL;
    if (type) {
        snprintf(type_pat, sizeof(type_pat), "*.%s", type);
        quoted_type = shell_quote_single_grep(type_pat);
    }

    char cmd[4096];
    int pos = 0;
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, "grep -rn%s", case_sensitive ? "" : "i");
    if (context_before > 0) pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -B %d", context_before);
    if (context_after > 0) pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -A %d", context_after);
    if (context > 0) pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -C %d", context);
    if (quoted_glob) pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --include=%s", quoted_glob);
    if (quoted_type) pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --include=%s", quoted_type);

    if (path) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s %s 2>/dev/null | head -%d", quoted_pattern, quoted_path, head_limit);
    } else {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s . 2>/dev/null --exclude-dir=.git --exclude-dir=node_modules | head -%d", quoted_pattern, head_limit);
    }

    free(quoted_pattern);
    free(quoted_path);
    free(quoted_glob);
    free(quoted_type);

    FILE *f = popen(cmd, "r");
    if (!f) {
        cJSON_Delete(json);
        return strdup("Error: grep failed");
    }

    StrBuf out = strbuf_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) strbuf_append(&out, buf);
    pclose(f);

    if (out.len == 0) strbuf_append(&out, "No matches found.");
    char *result = strbuf_detach(&out);
    cJSON_Delete(json);
    (void)output_mode;
    return result;
}
