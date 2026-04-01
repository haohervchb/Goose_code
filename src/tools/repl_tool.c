#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_repl_tool(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *language = json_get_string(json, "language");
    const char *code = json_get_string(json, "code");
    cJSON_Delete(json);

    if (!code) return strdup("Error: 'code' argument required");

    const char *interpreter = "python3";
    if (language) {
        if (strcmp(language, "python") == 0 || strcmp(language, "python3") == 0) interpreter = "python3";
        else if (strcmp(language, "node") == 0 || strcmp(language, "javascript") == 0) interpreter = "node";
        else if (strcmp(language, "ruby") == 0) interpreter = "ruby";
        else if (strcmp(language, "perl") == 0) interpreter = "perl";
    }

    char tmpfile[] = "/tmp/goosecode_repl_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd == -1) return strdup("Error: cannot create temp file");
    write(fd, code, strlen(code));
    close(fd);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", interpreter, tmpfile);
    FILE *f = popen(cmd, "r");
    if (!f) { unlink(tmpfile); return strdup("Error: interpreter not found"); }

    StrBuf out = strbuf_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) strbuf_append(&out, buf);
    pclose(f);
    unlink(tmpfile);

    if (out.len == 0) strbuf_append(&out, "(no output)");
    return strbuf_detach(&out);
}
