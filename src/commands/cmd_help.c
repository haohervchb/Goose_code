#include "commands/commands.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_help_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg; (void)sess;
    StrBuf out = strbuf_new();
    strbuf_append(&out, "Available commands:\n");
    strbuf_append(&out, "  /help              Show this help message\n");
    strbuf_append(&out, "  /exit              Exit the REPL\n");
    strbuf_append(&out, "  /clear             Clear the screen\n");
    strbuf_append(&out, "  /model [name]      Show or set the model\n");
    strbuf_append(&out, "  /session [id]      Show, save, or load a session\n");
    strbuf_append(&out, "  /compact           Compact the conversation context\n");
    strbuf_append(&out, "  /plan [subcommand] Manage plan mode and stored plan\n");
    strbuf_append(&out, "  /config [key]      Show or inspect runtime configuration\n");
    strbuf_append(&out, "  /branch [subcommand] Show, list, create, or switch branches\n");
    strbuf_append(&out, "  /commit [message]  Create a git commit from current changes\n");
    strbuf_append(&out, "  /review            Review current git changes locally\n");
    strbuf_append(&out, "  /subagents [subcommand] Inspect and clean subagent state\n");
    strbuf_append(&out, "  /tasks [subcommand] Inspect and update tracked tasks\n");
    strbuf_append(&out, "  /permissions       Show current permission mode\n");
    strbuf_append(&out, "  /cost              Show token usage and cost\n");
    strbuf_append(&out, "  /tools             List available tools\n");
    strbuf_append(&out, "\nSlash commands are processed locally. "
                         "All other input is sent to the AI model.\n");
    return strbuf_detach(&out);
}

void cmd_help_register(CommandRegistry *reg) {
    Command cmd = {strdup("help"), strdup("Show help message"), 0, cmd_help_exec};
    command_registry_register(reg, cmd);
}
