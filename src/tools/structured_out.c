#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_structured_out(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *schema = json_get_string(json, "schema");
    const char *data = json_get_string(json, "data");
    cJSON_Delete(json);

    StrBuf out = strbuf_new();
    if (data) {
        cJSON *parsed = cJSON_Parse(data);
        if (parsed) {
            char *pretty = cJSON_Print(parsed);
            strbuf_append(&out, pretty);
            free(pretty);
            cJSON_Delete(parsed);
        } else {
            strbuf_append(&out, data);
        }
    } else {
        strbuf_append(&out, "No data provided.");
    }
    if (schema) {
        strbuf_append_fmt(&out, "\n\nSchema: %s", schema);
    }
    return strbuf_detach(&out);
}
