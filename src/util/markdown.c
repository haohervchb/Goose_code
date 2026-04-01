#include "util/markdown.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static int starts_with(const char *s, const char *pfx) {
    return strncmp(s, pfx, strlen(pfx)) == 0;
}

char *markdown_to_text(const char *md) {
    if (!md) return strdup("");
    StrBuf out = strbuf_new();
    const char *line = md;
    int in_code = 0;

    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);

        char *ln = malloc(len + 1);
        memcpy(ln, line, len);
        ln[len] = '\0';

        if (starts_with(ln, "```")) {
            in_code = !in_code;
            strbuf_append(&out, in_code ? "\n" : "\n");
            free(ln);
        } else if (in_code) {
            strbuf_append(&out, "  ");
            strbuf_append(&out, ln);
            strbuf_append_char(&out, '\n');
            free(ln);
        } else if (starts_with(ln, "### ")) {
            strbuf_append_fmt(&out, "    %s\n", ln + 4);
            free(ln);
        } else if (starts_with(ln, "## ")) {
            strbuf_append_fmt(&out, "  %s\n", ln + 3);
            free(ln);
        } else if (starts_with(ln, "# ")) {
            strbuf_append_fmt(&out, "%s\n", ln + 2);
            free(ln);
        } else if (starts_with(ln, "- ") || starts_with(ln, "* ")) {
            strbuf_append_fmt(&out, "  • %s\n", ln + 2);
            free(ln);
        } else if (starts_with(ln, "> ")) {
            strbuf_append_fmt(&out, "  | %s\n", ln + 2);
            free(ln);
        } else if (ln[0] == '\0') {
            strbuf_append_char(&out, '\n');
            free(ln);
        } else {
            strbuf_append(&out, ln);
            strbuf_append_char(&out, '\n');
            free(ln);
        }

        line = end ? end + 1 : "";
    }
    return strbuf_detach(&out);
}

void markdown_print(const char *md) {
    char *txt = markdown_to_text(md);
    printf("%s", txt);
    free(txt);
}
