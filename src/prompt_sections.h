#ifndef PROMPT_SECTIONS_H
#define PROMPT_SECTIONS_H

#include "config.h"
#include "session.h"
#include <stddef.h>

typedef char *(*PromptSectionComputeFn)(const GooseConfig *cfg, const Session *sess,
                                        const char *working_dir);

typedef struct {
    const char *name;
    PromptSectionComputeFn compute;
    int cache_break;
} PromptSection;

char *prompt_sections_resolve(const PromptSection *sections, size_t count,
                              const GooseConfig *cfg, const Session *sess,
                              const char *working_dir);
void prompt_sections_clear_cache(void);
int prompt_sections_cache_size(void);

#endif
