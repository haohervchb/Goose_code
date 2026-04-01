#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define STRBUF_INIT_CAP 256

StrBuf strbuf_new(void) {
    StrBuf sb = {NULL, 0, 0};
    sb.cap = STRBUF_INIT_CAP;
    sb.data = calloc(1, sb.cap);
    return sb;
}

StrBuf strbuf_from(const char *s) {
    StrBuf sb = strbuf_new();
    if (s) strbuf_append(&sb, s);
    return sb;
}

static void strbuf_ensure(StrBuf *sb, size_t need) {
    if (sb->cap >= need) return;
    size_t new_cap = sb->cap * 2;
    while (new_cap < need) new_cap *= 2;
    sb->data = realloc(sb->data, new_cap);
    sb->cap = new_cap;
}

void strbuf_append(StrBuf *sb, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    strbuf_ensure(sb, sb->len + slen + 1);
    memcpy(sb->data + sb->len, s, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

void strbuf_append_len(StrBuf *sb, const char *s, size_t len) {
    if (!s || len == 0) return;
    strbuf_ensure(sb, sb->len + len + 1);
    memcpy(sb->data + sb->len, s, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void strbuf_append_char(StrBuf *sb, char c) {
    strbuf_ensure(sb, sb->len + 2);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void strbuf_append_fmt(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need <= 0) return;
    strbuf_ensure(sb, sb->len + (size_t)need + 1);
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)need + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)need;
}

void strbuf_clear(StrBuf *sb) {
    if (sb->data) sb->data[0] = '\0';
    sb->len = 0;
}

char *strbuf_detach(StrBuf *sb) {
    char *p = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return p;
}

void strbuf_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_trim(StrBuf *sb) {
    if (!sb->data || sb->len == 0) return;
    size_t start = 0;
    while (start < sb->len && isspace((unsigned char)sb->data[start])) start++;
    size_t end = sb->len;
    while (end > start && isspace((unsigned char)sb->data[end - 1])) end--;
    if (start > 0) {
        memmove(sb->data, sb->data + start, end - start);
    }
    sb->len = end - start;
    sb->data[sb->len] = '\0';
}

char *strbuf_trim_copy(const char *s) {
    if (!s) return strdup("");
    while (isspace((unsigned char)*s)) s++;
    const char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    size_t len = (size_t)(end - s);
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}
