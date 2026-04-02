#include "tools/tools.h"
#include "util/json_util.h"
#include "util/http.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *html_to_text(const char *html) {
    StrBuf out = strbuf_new();
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    const char *p = html;

    while (*p) {
        if (*p == '<') {
            in_tag = 1;
            if (strncasecmp(p, "<script", 7) == 0) in_script = 1;
            if (strncasecmp(p, "<style", 6) == 0) in_style = 1;
            if (strncasecmp(p, "<br", 3) == 0 || strncasecmp(p, "<p", 2) == 0 ||
                strncasecmp(p, "<div", 4) == 0 || strncasecmp(p, "<h", 2) == 0) {
                strbuf_append_char(&out, '\n');
            }
            if (strncasecmp(p, "<li", 3) == 0) strbuf_append(&out, "  - ");
            p++;
            continue;
        }
        if (*p == '>') {
            in_tag = 0;
            if (strncasecmp(p - 8, "</script", 8) == 0) in_script = 0;
            if (strncasecmp(p - 7, "</style", 7) == 0) in_style = 0;
            p++;
            continue;
        }
        if (!in_tag && !in_script && !in_style) {
            strbuf_append_char(&out, *p);
        }
        p++;
    }
    return strbuf_detach(&out);
}

char *tool_execute_web_fetch(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *url = json_get_string(json, "url");
    const char *prompt = json_get_string(json, "prompt");

    if (!url) {
        cJSON_Delete(json);
        return strdup("Error: 'url' argument required");
    }

    HttpResponse resp = http_get(url, NULL);
    if (resp.error) {
        char err[512];
        snprintf(err, sizeof(err), "Error fetching %s: %s", url, resp.error);
        http_response_free(&resp);
        cJSON_Delete(json);
        return strdup(err);
    }

    char status_info[64];
    snprintf(status_info, sizeof(status_info), "HTTP %ld", resp.status_code);

    if (resp.status_code != 200) {
        char err[256];
        snprintf(err, sizeof(err), "HTTP %ld from %s", resp.status_code, url);
        http_response_free(&resp);
        cJSON_Delete(json);
        return strdup(err);
    }

    char *text = html_to_text(resp.body.data);
    http_response_free(&resp);

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "URL: %s\nStatus: %s\n\n", url, status_info);

    if (prompt) {
        strbuf_append_fmt(&out, "User prompt: %s\n\nExtracted content:\n", prompt);
    }

    if (strlen(text) > 50000) {
        text[50000] = '\0';
        strbuf_append(&out, text);
        strbuf_append(&out, "\n\n[Content truncated at 50KB]");
    } else {
        strbuf_append(&out, text);
    }
    free(text);
    char *result = strbuf_detach(&out);
    cJSON_Delete(json);
    return result;
}
