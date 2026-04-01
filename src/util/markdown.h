#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "util/strbuf.h"

char *markdown_to_text(const char *md);
void markdown_print(const char *md);

#endif
