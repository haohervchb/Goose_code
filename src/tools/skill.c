#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

char *tool_execute_skill(const char *args, const GooseConfig *cfg) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *name = json_get_string(json, "name");
    cJSON_Delete(json);

    if (!name) return strdup("Error: 'name' argument required");

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.md", cfg->skill_dir, name);
    char *content = json_read_file(path);
    if (content) return content;

    snprintf(path, sizeof(path), "%s/%s.sh", cfg->skill_dir, name);
    content = json_read_file(path);
    if (content) return content;

    StrBuf out = strbuf_from("Available skills:\n");
    DIR *dir = opendir(cfg->skill_dir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            strbuf_append_fmt(&out, "  - %s\n", ent->d_name);
        }
        closedir(dir);
    } else {
        strbuf_append(&out, "  (none)\n");
    }
    return strbuf_detach(&out);
}
