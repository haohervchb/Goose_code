#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include "util/cJSON.h"
#include <stddef.h>

char *json_to_string(const cJSON *item);
const char *json_get_string(const cJSON *obj, const char *key);
int json_get_int(const cJSON *obj, const char *key, int def);
cJSON *json_get_array(const cJSON *obj, const char *key);
cJSON *json_get_object(const cJSON *obj, const char *key);
cJSON *json_build_message(const char *role, const char *content);
cJSON *json_build_tool_message(const char *role, cJSON *tool_calls);
cJSON *json_build_tool_result(const char *tool_call_id, const char *content);
cJSON *json_build_tool_def(const char *name, const char *description, cJSON *parameters);
cJSON *json_build_tool_def_openai(const char *name, const char *description, cJSON *input_schema);
char *json_read_file(const char *path);
int json_write_file(const char *path, const cJSON *item);

#endif
