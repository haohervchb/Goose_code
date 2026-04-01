#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

StrBuf strbuf_new(void);
StrBuf strbuf_from(const char *s);
void strbuf_free(StrBuf *sb);
void strbuf_append(StrBuf *sb, const char *s);
void strbuf_append_len(StrBuf *sb, const char *s, size_t len);
void strbuf_append_char(StrBuf *sb, char c);
void strbuf_append_fmt(StrBuf *sb, const char *fmt, ...);
void strbuf_clear(StrBuf *sb);
char *strbuf_detach(StrBuf *sb);
void strbuf_trim(StrBuf *sb);
char *strbuf_trim_copy(const char *s);

#endif
