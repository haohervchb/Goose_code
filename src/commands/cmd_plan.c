#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_plan_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)cfg;
    StrBuf out = strbuf_new();

    if (!args || args[0] == '\0') {
        if (!sess->plan_mode) {
            session_set_plan_mode(sess, 1);
            strbuf_append(&out, "Plan mode enabled.\n");
        } else {
            strbuf_append(&out, "Plan mode is already enabled.\n");
        }
        if (sess->plan_content && sess->plan_content[0]) {
            strbuf_append_fmt(&out, "Current plan:\n%s\n", sess->plan_content);
        } else {
            strbuf_append(&out, "No plan written yet. Use /plan set <text> to store one.\n");
        }
        return strbuf_detach(&out);
    }

    if (strcmp(args, "off") == 0 || strcmp(args, "exit") == 0) {
        session_set_plan_mode(sess, 0);
        strbuf_append(&out, "Plan mode disabled.\n");
        return strbuf_detach(&out);
    }

    if (strcmp(args, "show") == 0) {
        strbuf_append_fmt(&out, "Plan mode: %s\n", sess->plan_mode ? "enabled" : "disabled");
        if (sess->plan_content && sess->plan_content[0]) {
            strbuf_append_fmt(&out, "Current plan:\n%s\n", sess->plan_content);
        } else {
            strbuf_append(&out, "No plan written yet.\n");
        }
        return strbuf_detach(&out);
    }

    if (strcmp(args, "clear") == 0) {
        session_clear_plan(sess);
        strbuf_append(&out, "Cleared current plan text.\n");
        return strbuf_detach(&out);
    }

    const char *plan_text = args;
    if (strncmp(args, "set ", 4) == 0) {
        plan_text = args + 4;
    }

    session_set_plan_mode(sess, 1);
    session_set_plan(sess, plan_text);
    strbuf_append(&out, "Plan mode enabled and plan updated.\n");
    strbuf_append_fmt(&out, "Current plan:\n%s\n", sess->plan_content ? sess->plan_content : "");
    return strbuf_detach(&out);
}

void cmd_plan_register(CommandRegistry *reg) {
    Command cmd = {strdup("plan"), strdup("Enable, inspect, or update plan mode"), 1, cmd_plan_exec};
    command_registry_register(reg, cmd);
}
