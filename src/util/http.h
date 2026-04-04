#ifndef HTTP_H
#define HTTP_H

#include "util/strbuf.h"

typedef struct {
    long status_code;
    StrBuf body;
    char *error;
} HttpResponse;

HttpResponse http_get(const char *url, const char *auth_token);
HttpResponse http_post(const char *url, const char *auth_token, const char *content_type, const char *body);
HttpResponse http_post_stream(const char *url, const char *auth_token, const char *body,
                              void (*on_chunk)(const char *chunk, size_t len, void *ctx),
                              void *ctx);
HttpResponse http_post_stream_interruptible(const char *url, const char *auth_token, const char *body,
                                           void (*on_chunk)(const char *chunk, size_t len, void *ctx),
                                           void *ctx, volatile int *abort_flag);
void http_response_free(HttpResponse *resp);
int http_preconnect(const char *base_url);

#endif
