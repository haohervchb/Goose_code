#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tool_execute_notebook_edit(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *notebook_path = json_get_string(json, "notebook_path");
    const char *notebook = notebook_path ? notebook_path : json_get_string(json, "notebook");
    const char *cell_id = json_get_string(json, "cell_id");
    const char *new_source = json_get_string(json, "new_source");
    const char *source = new_source ? new_source : json_get_string(json, "source");
    const char *cell_type = json_get_string(json, "cell_type");
    const char *edit_mode = json_get_string(json, "edit_mode");
    cJSON_Delete(json);

    if (!notebook) return strdup("Error: 'notebook_path' argument required");
    if (!source) return strdup("Error: 'new_source' argument required");

    const char *dot = strrchr(notebook, '.');
    if (!dot || strcmp(dot, ".ipynb") != 0) {
        char err[256];
        snprintf(err, sizeof(err), "Error: notebook must be a .ipynb file, got '%s'", notebook);
        return strdup(err);
    }

    FILE *f = fopen(notebook, "r");
    if (!f) {
        char err[512];
        snprintf(err, sizeof(err), "Error: cannot open notebook '%s'", notebook);
        return strdup(err);
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *nb_content = malloc(sz + 1);
    size_t rd = fread(nb_content, 1, sz, f);
    nb_content[rd] = '\0';
    fclose(f);

    cJSON *nb = cJSON_Parse(nb_content);
    free(nb_content);
    if (!nb) return strdup("Error: invalid notebook JSON");

    cJSON *cells = cJSON_GetObjectItem(nb, "cells");
    if (!cells || !cJSON_IsArray(cells)) {
        cJSON_Delete(nb);
        return strdup("Error: notebook has no cells");
    }

    int found = 0;
    int cell_idx = 0;
    cJSON *cell;
    cJSON_ArrayForEach(cell, cells) {
        if (cell_id) {
            const char *cid = json_get_string(cell, "id");
            if (cid && strcmp(cid, cell_id) == 0) {
                if (!edit_mode || strcmp(edit_mode, "replace") == 0) {
                    cJSON_ReplaceItemInObject(cell, "source", cJSON_CreateString(source));
                }
                if (cell_type) {
                    cJSON_ReplaceItemInObject(cell, "cell_type", cJSON_CreateString(cell_type));
                }
                found = 1;
                break;
            }
        } else {
            if (cell_idx == 0) {
                cJSON_ReplaceItemInObject(cell, "source", cJSON_CreateString(source));
                if (cell_type) cJSON_ReplaceItemInObject(cell, "cell_type", cJSON_CreateString(cell_type));
                found = 1;
                break;
            }
        }
        cell_idx++;
    }

    if (!found) {
        cJSON_Delete(nb);
        char err[512];
        snprintf(err, sizeof(err), "Error: cell '%s' not found in notebook", cell_id ? cell_id : "0");
        return strdup(err);
    }

    char *out_json = cJSON_Print(nb);
    cJSON_Delete(nb);

    f = fopen(notebook, "w");
    if (!f) { free(out_json); return strdup("Error: cannot write notebook"); }
    fputs(out_json, f);
    fclose(f);
    free(out_json);

    StrBuf result = strbuf_new();
    strbuf_append_fmt(&result, "Successfully edited cell '%s' in %s", cell_id ? cell_id : "0", notebook);
    return strbuf_detach(&result);
}
