#include "tools/tools.h"
#include "util/json_util.h"
#include "util/http.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static int domain_matches(const char *domain, const char **list, int count) {
    for (int i = 0; i < count; i++) {
        if (strstr(domain, list[i])) return 1;
    }
    return 0;
}

char *tool_execute_web_search(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *query = json_get_string(json, "query");
    cJSON *allowed = json_get_array(json, "allowed_domains");
    cJSON *blocked = json_get_array(json, "blocked_domains");
    cJSON_Delete(json);

    if (!query) return strdup("Error: 'query' argument required");

    const char *allowed_arr[32];
    int nallowed = 0;
    if (allowed) {
        cJSON *item;
        cJSON_ArrayForEach(item, allowed) {
            if (cJSON_IsString(item) && nallowed < 32) allowed_arr[nallowed++] = item->valuestring;
        }
    }

    const char *blocked_arr[32];
    int nblocked = 0;
    if (blocked) {
        cJSON *item;
        cJSON_ArrayForEach(item, blocked) {
            if (cJSON_IsString(item) && nblocked < 32) blocked_arr[nblocked++] = item->valuestring;
        }
    }

    char *encoded = curl_escape(query, 0);
    char url[1024];
    snprintf(url, sizeof(url), "https://html.duckduckgo.com/html/?q=%s", encoded);
    curl_free(encoded);

    HttpResponse resp = http_get(url, NULL);
    if (resp.error) {
        char err[512];
        snprintf(err, sizeof(err), "Search error: %s", resp.error);
        http_response_free(&resp);
        return strdup(err);
    }

    StrBuf out = strbuf_new();
    strbuf_append_fmt(&out, "Search results for: %s\n\n", query);

    const char *p = resp.body.data;
    int found = 0;
    while ((p = strstr(p, "result__url")) && found < 10) {
        p = strchr(p, '>');
        if (!p) break;
        p++;
        const char *end = strchr(p, '<');
        if (!end) break;

        if (nblocked > 0 && domain_matches(p, blocked_arr, nblocked)) {
            p = end;
            continue;
        }
        if (nallowed > 0 && !domain_matches(p, allowed_arr, nallowed)) {
            p = end;
            continue;
        }

        strbuf_append_fmt(&out, "- URL: %.*s\n", (int)(end - p), p);

        p = strstr(end, "result__snippet");
        if (!p) break;
        p = strchr(p, '>');
        if (!p) break;
        p++;
        end = strchr(p, '<');
        if (!end) break;
        strbuf_append_fmt(&out, "  %.*s\n\n", (int)(end - p), p);
        found++;
    }

    if (found == 0) strbuf_append(&out, "No results found.\n");
    http_response_free(&resp);
    return strbuf_detach(&out);
}
