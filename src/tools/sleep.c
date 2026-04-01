#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

char *tool_execute_sleep(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    int duration_ms = json_get_int(json, "duration_ms", 1000);
    cJSON_Delete(json);

    if (duration_ms < 0) duration_ms = 1000;
    if (duration_ms > 300000) duration_ms = 300000;

    usleep((unsigned int)duration_ms * 1000);

    StrBuf result = strbuf_new();
    strbuf_append_fmt(&result, "Slept for %dms", duration_ms);
    return strbuf_detach(&result);
}
