#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include "util/terminal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *description;
} QuestionOption;

static void trim_in_place(char *s) {
    size_t start = 0;
    size_t end = strlen(s);

    while (s[start] && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;

    if (start > 0) memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

static int parse_boolean(const cJSON *obj, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem((cJSON *)obj, key);
    if (!item) return default_value;
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return default_value;
}

static int parse_single_selection(char *input, QuestionOption *options, int option_count,
                                  int allow_custom, cJSON *selections, char **error_out) {
    trim_in_place(input);
    if (input[0] == '\0') {
        *error_out = strdup("Please enter a choice.");
        return -1;
    }

    char *end = NULL;
    long index = strtol(input, &end, 10);
    while (end && *end && isspace((unsigned char)*end)) end++;
    if (end != input && end && *end == '\0') {
        if (index >= 1 && index <= option_count) {
            cJSON_AddItemToArray(selections, cJSON_CreateString(options[index - 1].label));
            return 0;
        }
        if (allow_custom && index == option_count + 1) {
            *error_out = strdup("Type your own answer directly instead of selecting the custom option number.");
            return -1;
        }
        *error_out = strdup("Choice out of range.");
        return -1;
    }

    if (allow_custom) {
        cJSON_AddItemToArray(selections, cJSON_CreateString(input));
        return 0;
    }

    for (int i = 0; i < option_count; i++) {
        if (strcmp(input, options[i].label) == 0) {
            cJSON_AddItemToArray(selections, cJSON_CreateString(options[i].label));
            return 0;
        }
    }

    *error_out = strdup("Please enter a valid choice label or number.");
    return -1;
}

static int selection_exists(cJSON *selections, const char *label) {
    cJSON *item;
    cJSON_ArrayForEach(item, selections) {
        if (cJSON_IsString(item) && item->valuestring && strcmp(item->valuestring, label) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_multiple_selection(char *input, QuestionOption *options, int option_count,
                                    int allow_custom, cJSON *selections, char **error_out) {
    trim_in_place(input);
    if (input[0] == '\0') {
        *error_out = strdup("Please enter one or more comma-separated choices.");
        return -1;
    }

    char *saveptr = NULL;
    char *token = strtok_r(input, ",", &saveptr);
    while (token) {
        trim_in_place(token);
        if (token[0] != '\0') {
            char *end = NULL;
            long index = strtol(token, &end, 10);
            while (end && *end && isspace((unsigned char)*end)) end++;

            if (end != token && end && *end == '\0') {
                if (index >= 1 && index <= option_count) {
                    const char *label = options[index - 1].label;
                    if (!selection_exists(selections, label)) {
                        cJSON_AddItemToArray(selections, cJSON_CreateString(label));
                    }
                } else {
                    if (allow_custom && index == option_count + 1) {
                        *error_out = strdup("Type custom answers directly instead of selecting the custom option number.");
                        return -1;
                    }
                    if (!allow_custom) {
                        *error_out = strdup("One of the numeric choices is out of range.");
                        return -1;
                    }
                    if (!selection_exists(selections, token)) {
                        cJSON_AddItemToArray(selections, cJSON_CreateString(token));
                    }
                }
            } else {
                int matched = 0;
                for (int i = 0; i < option_count; i++) {
                    if (strcmp(token, options[i].label) == 0) {
                        if (!selection_exists(selections, options[i].label)) {
                            cJSON_AddItemToArray(selections, cJSON_CreateString(options[i].label));
                        }
                        matched = 1;
                        break;
                    }
                }
                if (!matched) {
                    if (!allow_custom) {
                        *error_out = strdup("One of the choices is not valid.");
                        return -1;
                    }
                    if (!selection_exists(selections, token)) {
                        cJSON_AddItemToArray(selections, cJSON_CreateString(token));
                    }
                }
            }
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    if (cJSON_GetArraySize(selections) == 0) {
        *error_out = strdup("Please enter at least one choice.");
        return -1;
    }

    return 0;
}

static void print_question(FILE *output, const char *header, const char *question,
                           QuestionOption *options, int option_count,
                           int multiple, int allow_custom) {
    if (header && header[0]) fprintf(output, "\n%s\n", header);
    fprintf(output, "%s\n", question);

    for (int i = 0; i < option_count; i++) {
        fprintf(output, "  %d. %s", i + 1, options[i].label);
        if (options[i].description && options[i].description[0]) {
            fprintf(output, " - %s", options[i].description);
        }
        fprintf(output, "\n");
    }

    if (allow_custom) {
        fprintf(output, "  %d. Type your own answer\n", option_count + 1);
    }

    fprintf(output, "%s: ", multiple ? "Enter one or more choices (comma-separated)" : "Enter one choice");
    fflush(output);
}

static int read_response_line(FILE *input, FILE *output, char *line, size_t line_size) {
    if (input == stdin && output == stdout) {
        char *response = term_read_line_opts("", 0, 0);
        if (!response) return 0;
        snprintf(line, line_size, "%s", response);
        free(response);
        return 1;
    }

    if (!fgets(line, (int)line_size, input)) return 0;
    line[strcspn(line, "\r\n")] = '\0';
    return 1;
}

char *tool_execute_ask_user_question_with_io(const char *args, FILE *input, FILE *output) {
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    cJSON *questions = json_get_array(json, "questions");
    if (!questions || cJSON_GetArraySize(questions) == 0) {
        cJSON_Delete(json);
        return strdup("Error: 'questions' array required");
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *answers = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "answers", answers);

    cJSON *question_item;
    cJSON_ArrayForEach(question_item, questions) {
        const char *question = json_get_string(question_item, "question");
        const char *header = json_get_string(question_item, "header");
        cJSON *options_json = json_get_array(question_item, "options");
        int multiple = parse_boolean(question_item, "multiple", 0);
        int allow_custom = parse_boolean(question_item, "custom", 1);

        if (!question || !options_json || cJSON_GetArraySize(options_json) == 0) {
            cJSON_Delete(result);
            cJSON_Delete(json);
            return strdup("Error: each question needs 'question' and non-empty 'options'");
        }

        int option_count = cJSON_GetArraySize(options_json);
        QuestionOption *options = calloc((size_t)option_count, sizeof(*options));
        if (!options) {
            cJSON_Delete(result);
            cJSON_Delete(json);
            return strdup("Error: out of memory");
        }

        int options_valid = 1;
        for (int i = 0; i < option_count; i++) {
            cJSON *option = cJSON_GetArrayItem(options_json, i);
            options[i].label = json_get_string(option, "label");
            options[i].description = json_get_string(option, "description");
            if (!options[i].label || options[i].label[0] == '\0') {
                options_valid = 0;
                break;
            }
        }

        if (!options_valid) {
            free(options);
            cJSON_Delete(result);
            cJSON_Delete(json);
            return strdup("Error: each option needs a non-empty 'label'");
        }

        char line[4096];
        cJSON *selections = cJSON_CreateArray();
        while (1) {
            print_question(output, header, question, options, option_count, multiple, allow_custom);
            if (!read_response_line(input, output, line, sizeof(line))) {
                free(options);
                cJSON_Delete(result);
                cJSON_Delete(json);
                return strdup("Error: no user response received");
            }

            char *work = strdup(line);
            char *error = NULL;
            int rc = multiple
                ? parse_multiple_selection(work, options, option_count, allow_custom, selections, &error)
                : parse_single_selection(work, options, option_count, allow_custom, selections, &error);
            free(work);

            if (rc == 0) break;

            cJSON_Delete(selections);
            selections = cJSON_CreateArray();
            fprintf(output, "%s\n", error ? error : "Invalid response.");
            fflush(output);
            free(error);
        }

        cJSON *answer = cJSON_CreateObject();
        if (header) cJSON_AddStringToObject(answer, "header", header);
        cJSON_AddStringToObject(answer, "question", question);
        cJSON_AddItemToObject(answer, "selections", selections);
        cJSON_AddItemToArray(answers, answer);
        free(options);
    }

    char *out = json_to_string(result);
    cJSON_Delete(result);
    cJSON_Delete(json);
    return out ? out : strdup("Error: failed to encode question result");
}

char *tool_execute_ask_user_question(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    return tool_execute_ask_user_question_with_io(args, stdin, stdout);
}
