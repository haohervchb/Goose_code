#include "util/json_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char *json_to_string(const cJSON *item) {
    return cJSON_PrintUnformatted(item);
}

const char *json_get_string(const cJSON *obj, const char *key) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsString(v)) return v->valuestring;
    return NULL;
}

int json_get_int(const cJSON *obj, const char *key, int def) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsNumber(v)) return v->valueint;
    return def;
}

cJSON *json_get_array(const cJSON *obj, const char *key) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsArray(v)) return v;
    return NULL;
}

cJSON *json_get_object(const cJSON *obj, const char *key) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsObject(v)) return v;
    return NULL;
}

cJSON *json_build_message(const char *role, const char *content) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddStringToObject(msg, "content", content ? content : "");
    return msg;
}

cJSON *json_build_tool_message(const char *role, cJSON *tool_calls) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddItemToObject(msg, "tool_calls", tool_calls);
    cJSON_AddStringToObject(msg, "content", "");
    return msg;
}

cJSON *json_build_tool_result(const char *tool_call_id, const char *content) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "tool");
    cJSON_AddStringToObject(msg, "tool_call_id", tool_call_id);
    cJSON_AddStringToObject(msg, "content", content ? content : "");
    return msg;
}

cJSON *json_build_tool_def(const char *name, const char *description, cJSON *parameters) {
    cJSON *def = cJSON_CreateObject();
    cJSON_AddStringToObject(def, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", name);
    cJSON_AddStringToObject(func, "description", description);
    if (parameters && cJSON_IsObject(parameters)) {
        cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(parameters, 1));
    } else {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON_AddItemToObject(params, "properties", cJSON_CreateObject());
        cJSON_AddItemToObject(func, "parameters", params);
    }
    cJSON_AddItemToObject(def, "function", func);
    return def;
}

cJSON *json_build_tool_def_openai(const char *name, const char *description, cJSON *input_schema) {
    cJSON *def = cJSON_CreateObject();
    cJSON_AddStringToObject(def, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", name);
    cJSON_AddStringToObject(func, "description", description ? description : "");
    if (input_schema && cJSON_IsObject(input_schema)) {
        cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(input_schema, 1));
    } else {
        cJSON *schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON_AddItemToObject(schema, "properties", cJSON_CreateObject());
        cJSON_AddItemToObject(func, "parameters", schema);
    }
    cJSON_AddItemToObject(def, "function", func);
    return def;
}

char *json_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

int json_write_file(const char *path, const cJSON *item) {
    char *s = cJSON_Print(item);
    if (!s) return -1;
    FILE *f = fopen(path, "w");
    if (!f) { free(s); return -1; }
    fputs(s, f);
    fclose(f);
    free(s);
    return 0;
}
