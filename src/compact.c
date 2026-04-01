#include "compact.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *compact_summarize(const char *messages_json, int keep_recent) {
    (void)messages_json;
    (void)keep_recent;
    return strdup("[Conversation context compacted]");
}
