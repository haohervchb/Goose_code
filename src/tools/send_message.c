#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_send_message(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *message = json_get_string(json, "message");
    const char *status = json_get_string(json, "status");
    cJSON *attachments = json_get_array(json, "attachments");

    if (!message) {
        cJSON_Delete(json);
        return strdup("Error: 'message' argument required");
    }

    StrBuf out = strbuf_new();
    if (status) strbuf_append_fmt(&out, "[%s] ", status);
    strbuf_append(&out, message);

    if (attachments && cJSON_IsArray(attachments)) {
        cJSON *item;
        strbuf_append(&out, "\n\nAttachments:");
        cJSON_ArrayForEach(item, attachments) {
            const char *path = json_get_string(item, "path");
            if (path) strbuf_append_fmt(&out, "\n  - %s", path);
        }
    }

    printf("\n%s\n", out.data);
    fflush(stdout);

    char *result = strdup("Message sent to user.");
    strbuf_free(&out);
    cJSON_Delete(json);
    return result;
}
