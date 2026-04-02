#include "prompt_sections.h"
#include "util/strbuf.h"
#include <stdlib.h>
#include <string.h>

char *prompt_sections_resolve(const PromptSection *sections, size_t count,
                              const GooseConfig *cfg, const Session *sess,
                              const char *working_dir) {
    StrBuf out = strbuf_new();

    for (size_t i = 0; i < count; i++) {
        if (!sections[i].compute) continue;
        char *content = sections[i].compute(cfg, sess, working_dir);
        if (!content || !content[0]) {
            free(content);
            continue;
        }

        if (out.len > 0 && out.data[out.len - 1] != '\n') {
            strbuf_append_char(&out, '\n');
        }
        strbuf_append(&out, content);
        if (out.len == 0 || out.data[out.len - 1] != '\n') {
            strbuf_append_char(&out, '\n');
        }
        free(content);
    }

    return strbuf_detach(&out);
}
