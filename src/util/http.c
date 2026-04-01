#include "util/http.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    StrBuf body;
    void (*on_chunk)(const char *, size_t, void *);
    void *ctx;
} CurlCtx;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    CurlCtx *c = (CurlCtx *)userdata;
    strbuf_append_len(&c->body, (const char *)ptr, total);
    if (c->on_chunk) {
        c->on_chunk((const char *)ptr, total, c->ctx);
    }
    return total;
}

static HttpResponse do_request(CURL *curl, CurlCtx *ctx) {
    HttpResponse resp = {0, strbuf_new(), NULL};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        resp.error = strdup(curl_easy_strerror(rc));
        return resp;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    return resp;
}

HttpResponse http_get(const char *url, const char *auth_token) {
    CURL *curl = curl_easy_init();
    CurlCtx ctx = {strbuf_new(), NULL, NULL};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    struct curl_slist *headers = NULL;
    if (auth_token) {
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", auth_token);
        headers = curl_slist_append(headers, auth);
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    HttpResponse resp = do_request(curl, &ctx);
    resp.body = ctx.body;
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse http_post(const char *url, const char *auth_token, const char *content_type, const char *body) {
    CURL *curl = curl_easy_init();
    CurlCtx ctx = {strbuf_new(), NULL, NULL};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    struct curl_slist *headers = NULL;
    if (auth_token) {
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", auth_token);
        headers = curl_slist_append(headers, auth);
    }
    if (content_type) {
        char ct[256];
        snprintf(ct, sizeof(ct), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, ct);
    } else {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    HttpResponse resp = do_request(curl, &ctx);
    resp.body = ctx.body;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse http_post_stream(const char *url, const char *auth_token, const char *body,
                              void (*on_chunk)(const char *, size_t, void *), void *ctx) {
    CURL *curl = curl_easy_init();
    CurlCtx cctx = {strbuf_new(), on_chunk, ctx};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    struct curl_slist *headers = NULL;
    if (auth_token) {
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", auth_token);
        headers = curl_slist_append(headers, auth);
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    HttpResponse resp = do_request(curl, &cctx);
    resp.body = cctx.body;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

void http_response_free(HttpResponse *resp) {
    strbuf_free(&resp->body);
    free(resp->error);
    resp->error = NULL;
}
