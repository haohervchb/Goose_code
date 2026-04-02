#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>

char *tool_execute_glob_search(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *pattern = json_get_string(json, "pattern");
    const char *path = json_get_string(json, "path");

    if (!pattern) {
        cJSON_Delete(json);
        return strdup("Error: 'pattern' argument required");
    }

    char full_pattern[2048];
    if (path) snprintf(full_pattern, sizeof(full_pattern), "%s/%s", path, pattern);
    else snprintf(full_pattern, sizeof(full_pattern), "./%s", pattern);

    glob_t results;
    int rc = glob(full_pattern, GLOB_ERR | GLOB_NOSORT, NULL, &results);
    if (rc == GLOB_NOMATCH) {
        cJSON_Delete(json);
        return strdup("No files matching pattern.");
    }
    if (rc != 0) {
        char err[256];
        snprintf(err, sizeof(err), "Error: glob failed (%d)", rc);
        cJSON_Delete(json);
        return strdup(err);
    }

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Found %zu matches:\n", results.gl_pathc);
    for (size_t i = 0; i < results.gl_pathc && i < 200; i++) {
        strbuf_append_fmt(&out, "  %s\n", results.gl_pathv[i]);
    }
    if (results.gl_pathc > 200) strbuf_append_fmt(&out, "  ... and %zu more\n", results.gl_pathc - 200);
    globfree(&results);
    char *result = strbuf_detach(&out);
    cJSON_Delete(json);
    return result;
}
