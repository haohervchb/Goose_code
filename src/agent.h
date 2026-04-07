#ifndef AGENT_H
#define AGENT_H

#include "config.h"
#include "session.h"
#include "tools/tools.h"
#include "commands/commands.h"
#include "api.h"

typedef struct {
    GooseConfig config;
    Session *session;
    ToolRegistry tools;
    CommandRegistry commands;
    ApiConfig api_cfg;
    cJSON *system_message;
    int running;
    int tui_mode;
    long total_input_tokens;
    long total_output_tokens;
    
    // TUI mode callbacks (optional)
    void (*on_text)(const char *text, void *ctx);
    void (*on_tool_start)(const char *id, const char *name, const char *args, void *ctx);
    void (*on_tool_output)(const char *id, const char *output, void *ctx);
    void (*on_tool_done)(const char *id, int success, const char *error, void *ctx);
    void *callback_ctx;
} Agent;

Agent *agent_init(const char *working_dir);
void agent_free(Agent *agent);
int agent_run_turn(Agent *agent, const char *user_input);
int agent_run_repl(Agent *agent);
char *agent_process_command(Agent *agent, const char *cmd_name, const char *cmd_args);
Agent *agent_current(void);

void agent_set_callbacks(Agent *agent, 
    void (*on_text)(const char *, void *),
    void (*on_tool_start)(const char *, const char *, const char *, void *),
    void (*on_tool_output)(const char *, const char *, void *),
    void (*on_tool_done)(const char *, int, const char *, void *),
    void *ctx);

#endif
