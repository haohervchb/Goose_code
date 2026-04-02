#include "commands/commands.h"
#include "tools/tools.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cmd_tools_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg; (void)sess;
    StrBuf out = strbuf_new();
    strbuf_append(&out, "Available tools:\n");
    strbuf_append(&out, "  bash            - Execute shell commands\n");
    strbuf_append(&out, "  read_file       - Read file contents\n");
    strbuf_append(&out, "  write_file      - Write/create files\n");
    strbuf_append(&out, "  edit_file       - Edit files with string replacement\n");
    strbuf_append(&out, "  glob_search     - Find files by pattern\n");
    strbuf_append(&out, "  grep_search     - Search file contents\n");
    strbuf_append(&out, "  web_fetch       - Fetch URL content\n");
    strbuf_append(&out, "  web_search      - Search the web\n");
    strbuf_append(&out, "  todo_write       - Manage todo list\n");
    strbuf_append(&out, "  skill           - Load skill files\n");
    strbuf_append(&out, "  agent           - Spawn sub-agents\n");
    strbuf_append(&out, "  tool_search     - List available tools\n");
    strbuf_append(&out, "  notebook_edit   - Edit Jupyter notebooks\n");
    strbuf_append(&out, "  sleep           - Wait for a duration\n");
    strbuf_append(&out, "  send_message    - Send message to user\n");
    strbuf_append(&out, "  ask_user_question - Ask the user a structured question\n");
    strbuf_append(&out, "  enter_plan_mode - Enable plan mode and optionally store a plan\n");
    strbuf_append(&out, "  exit_plan_mode  - Disable plan mode\n");
    strbuf_append(&out, "  config          - Manage configuration\n");
    strbuf_append(&out, "  structured_output - Format structured output\n");
    strbuf_append(&out, "  repl            - Execute code in REPL\n");
    strbuf_append(&out, "  powershell      - Execute PowerShell commands\n");
    return strbuf_detach(&out);
}

void cmd_tools_register(CommandRegistry *reg) {
    Command cmd = {strdup("tools"), strdup("List available tools"), 0, cmd_tools_exec};
    command_registry_register(reg, cmd);
}
