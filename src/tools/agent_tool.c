#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *tool_execute_agent_tool(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *prompt = json_get_string(json, "prompt");
    const char *name = json_get_string(json, "name");
    const char *description = json_get_string(json, "description");
    const char *subagent_type = json_get_string(json, "subagent_type");
    const char *model = json_get_string(json, "model");
    cJSON_Delete(json);

    if (!prompt) return strdup("Error: 'prompt' argument required");
    if (!description) return strdup("Error: 'description' argument required");

    char agent_id[64];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(agent_id, sizeof(agent_id), "agent_%ld_%ld", (long)ts.tv_sec, (long)ts.tv_nsec);

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Sub-agent '%s' (ID: %s) spawned\n", name ? name : agent_id, agent_id);
    strbuf_append_fmt(&out, "Description: %s\n", description);
    strbuf_append_fmt(&out, "Type: %s\n", subagent_type ? subagent_type : "general");
    if (model) strbuf_append_fmt(&out, "Model: %s\n", model);
    strbuf_append_fmt(&out, "Prompt: %s\n", prompt);
    strbuf_append(&out, "\n[Note: Sub-agent execution would run as a separate process in a full implementation. "
                         "The sub-agent manifest and output files would be persisted for later retrieval.]");
    return strbuf_detach(&out);
}
