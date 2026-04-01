#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *tool_execute_powershell(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *command = json_get_string(json, "command");
    int timeout = json_get_int(json, "timeout", 120);
    cJSON_Delete(json);

    if (!command) return strdup("Error: 'command' argument required");

    if (access("/usr/bin/pwsh", F_OK) != 0 && access("/usr/bin/powershell", F_OK) != 0) {
        return strdup("Error: PowerShell not found. Install pwsh to use this tool.");
    }

    const char *ps = access("/usr/bin/pwsh", F_OK) == 0 ? "/usr/bin/pwsh" : "/usr/bin/powershell";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s -Command '%s' 2>&1", ps, command);

    FILE *f = popen(cmd, "r");
    if (!f) return strdup("Error: PowerShell execution failed");

    StrBuf out = strbuf_new();
    char buf[4096];
    int elapsed = 0;
    while (fgets(buf, sizeof(buf), f)) {
        strbuf_append(&out, buf);
        elapsed++;
        if (elapsed > timeout * 100) break;
    }
    pclose(f);

    if (out.len == 0) strbuf_append(&out, "(no output)");
    return strbuf_detach(&out);
}
