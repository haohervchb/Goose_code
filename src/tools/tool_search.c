#include "tools/tools.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *name;
    const char *description;
    int score;
} ToolScore;

static int tokenize(const char *s, char tokens[][64], int max_tokens) {
    int count = 0;
    const char *p = s;
    while (*p && count < max_tokens) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        int len = 0;
        while (*p && !isspace((unsigned char)*p) && len < 63) {
            tokens[count][len++] = tolower((unsigned char)*p++);
        }
        tokens[count][len] = '\0';
        count++;
    }
    return count;
}

char *tool_execute_tool_search(const char *args, const GooseConfig *cfg) {
    (void)cfg;
    cJSON *json = cJSON_Parse(args);
    if (!json) return strdup("Error: invalid JSON arguments");

    const char *query = json_get_string(json, "query");
    int max_results = json_get_int(json, "max_results", 20);
    cJSON_Delete(json);

    typedef struct { const char *name; const char *desc; } ToolInfo;
    ToolInfo tools[] = {
        {"bash", "Execute shell commands with timeout support"},
        {"read_file", "Read file contents with line range support"},
        {"write_file", "Create or overwrite files"},
        {"edit_file", "Edit files with precise string replacement"},
        {"glob_search", "Find files matching glob patterns"},
        {"grep_search", "Search file contents with regex, context lines, and type filtering"},
        {"web_fetch", "Fetch URL content with prompt-aware extraction"},
        {"web_search", "Search the web via DuckDuckGo with domain filtering"},
        {"todo_write", "Manage todo list with status tracking"},
        {"skill", "Load skill files from the skill directory"},
        {"agent", "Spawn sub-agents for delegated tasks"},
        {"tool_search", "List and search available tools"},
        {"notebook_edit", "Edit cells in Jupyter notebooks"},
        {"sleep", "Wait for a specified duration"},
        {"send_message", "Send messages to the user with attachments"},
        {"ask_user_question", "Ask the user structured questions and wait for a reply"},
        {"config", "View or modify runtime configuration"},
        {"structured_output", "Format structured output responses"},
        {"repl", "Execute code in a REPL (Python, Node.js, Ruby, Perl)"},
        {"powershell", "Execute PowerShell commands"}
    };
    int ntools = sizeof(tools) / sizeof(tools[0]);

    ToolScore scores[32];
    int nscores = 0;

    if (!query) {
        for (int i = 0; i < ntools && nscores < 32; i++) {
            scores[nscores].name = tools[i].name;
            scores[nscores].description = tools[i].desc;
            scores[nscores].score = 0;
            nscores++;
        }
    } else {
        char qtokens[16][64];
        int nqtokens = tokenize(query, qtokens, 16);

        for (int i = 0; i < ntools && nscores < 32; i++) {
            int score = 0;
            const char *haystacks[] = {tools[i].name, tools[i].desc, NULL};

            for (int t = 0; t < nqtokens; t++) {
                for (int h = 0; haystacks[h]; h++) {
                    const char *pos = strcasestr(haystacks[h], qtokens[t]);
                    if (pos) {
                        if (pos == haystacks[h]) score += 10;
                        else score += 5;
                    }
                }
            }

            if (strncmp(query, "select:", 7) == 0) {
                const char *exact = query + 7;
                if (strcmp(tools[i].name, exact) == 0) score = 1000;
            }

            if (score > 0) {
                scores[nscores].name = tools[i].name;
                scores[nscores].description = tools[i].desc;
                scores[nscores].score = score;
                nscores++;
            }
        }

        for (int i = 0; i < nscores - 1; i++) {
            for (int j = i + 1; j < nscores; j++) {
                if (scores[j].score > scores[i].score) {
                    ToolScore tmp = scores[i];
                    scores[i] = scores[j];
                    scores[j] = tmp;
                }
            }
        }

        if (nscores > max_results) nscores = max_results;
    }

    StrBuf out = strbuf_new();
    if (nscores == 0) {
        strbuf_append_fmt(&out, "No tools found matching query: %s\n", query ? query : "");
    } else {
        strbuf_append_fmt(&out, "Found %d tool%s:\n\n", nscores, nscores == 1 ? "" : "s");
        for (int i = 0; i < nscores; i++) {
            strbuf_append_fmt(&out, "  %-20s %s\n", scores[i].name, scores[i].description);
        }
    }
    return strbuf_detach(&out);
}
