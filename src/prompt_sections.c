#include "prompt_sections.h"
#include "util/strbuf.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    char *value;
} PromptSectionCacheEntry;

static PromptSectionCacheEntry *g_prompt_section_cache = NULL;
static size_t g_prompt_section_cache_count = 0;
static size_t g_prompt_section_cache_cap = 0;

static const char *prompt_section_cache_get(const char *name) {
    for (size_t i = 0; i < g_prompt_section_cache_count; i++) {
        if (strcmp(g_prompt_section_cache[i].name, name) == 0) {
            return g_prompt_section_cache[i].value;
        }
    }
    return NULL;
}

static void prompt_section_cache_put(const char *name, const char *value) {
    for (size_t i = 0; i < g_prompt_section_cache_count; i++) {
        if (strcmp(g_prompt_section_cache[i].name, name) == 0) {
            free(g_prompt_section_cache[i].value);
            g_prompt_section_cache[i].value = strdup(value ? value : "");
            return;
        }
    }

    if (g_prompt_section_cache_count + 1 > g_prompt_section_cache_cap) {
        size_t new_cap = g_prompt_section_cache_cap ? g_prompt_section_cache_cap * 2 : 8;
        g_prompt_section_cache = realloc(g_prompt_section_cache, new_cap * sizeof(*g_prompt_section_cache));
        g_prompt_section_cache_cap = new_cap;
    }

    g_prompt_section_cache[g_prompt_section_cache_count].name = strdup(name);
    g_prompt_section_cache[g_prompt_section_cache_count].value = strdup(value ? value : "");
    g_prompt_section_cache_count++;
}

void prompt_sections_clear_cache(void) {
    for (size_t i = 0; i < g_prompt_section_cache_count; i++) {
        free(g_prompt_section_cache[i].name);
        free(g_prompt_section_cache[i].value);
    }
    free(g_prompt_section_cache);
    g_prompt_section_cache = NULL;
    g_prompt_section_cache_count = 0;
    g_prompt_section_cache_cap = 0;
}

int prompt_sections_cache_size(void) {
    return (int)g_prompt_section_cache_count;
}

char *prompt_sections_resolve(const PromptSection *sections, size_t count,
                              const GooseConfig *cfg, const Session *sess,
                              const char *working_dir) {
    StrBuf out = strbuf_new();

    for (size_t i = 0; i < count; i++) {
        if (!sections[i].compute) continue;
        char *content = NULL;
        if (!sections[i].cache_break) {
            const char *cached = prompt_section_cache_get(sections[i].name);
            if (cached) content = strdup(cached);
        }
        if (!content) {
            content = sections[i].compute(cfg, sess, working_dir);
            if (!sections[i].cache_break) {
                prompt_section_cache_put(sections[i].name, content ? content : "");
            }
        }
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
