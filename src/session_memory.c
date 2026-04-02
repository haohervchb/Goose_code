#include "session_memory.h"
#include "util/json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *session_memory_default_template(void) {
    return strdup(
        "# Session Title\n"
        "_A short and distinctive 5-10 word descriptive title for the session._\n\n"
        "# Current State\n"
        "_What is actively being worked on right now? Pending tasks and immediate next steps._\n\n"
        "# Task Specification\n"
        "_What did the user ask to build? Include design decisions and explanatory context._\n\n"
        "# Files and Functions\n"
        "_What are the important files and why are they relevant?_\n\n"
        "# Workflow\n"
        "_What commands are usually run and in what order?_\n\n"
        "# Errors and Corrections\n"
        "_Errors encountered, how they were fixed, and what approaches failed._\n\n"
        "# Codebase and System Documentation\n"
        "_Important components and how they fit together._\n\n"
        "# Learnings\n"
        "_What worked well? What should be avoided?_\n\n"
        "# Key Results\n"
        "_Repeat any exact answer, table, or deliverable the user asked for._\n\n"
        "# Worklog\n"
        "_Step-by-step terse log of what was attempted and done._\n");
}

char *session_memory_path(const GooseConfig *cfg, const Session *sess) {
    size_t len = strlen(cfg->session_memory_dir) + strlen(sess->id) + 8;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s.md", cfg->session_memory_dir, sess->id);
    return path;
}

int session_memory_ensure(const GooseConfig *cfg, const Session *sess) {
    char *path = session_memory_path(cfg, sess);
    char *existing = json_read_file(path);
    if (existing) {
        free(existing);
        free(path);
        return 0;
    }

    char *tmpl = session_memory_default_template();
    FILE *f = fopen(path, "w");
    if (!f) {
        free(tmpl);
        free(path);
        return -1;
    }
    fputs(tmpl, f);
    fclose(f);
    free(tmpl);
    free(path);
    return 0;
}

char *session_memory_load(const GooseConfig *cfg, const Session *sess) {
    char *path = session_memory_path(cfg, sess);
    char *content = json_read_file(path);
    free(path);
    return content;
}
