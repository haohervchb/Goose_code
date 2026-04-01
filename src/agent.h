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
    long total_input_tokens;
    long total_output_tokens;
} Agent;

Agent *agent_init(const char *working_dir);
void agent_free(Agent *agent);
int agent_run_turn(Agent *agent, const char *user_input);
int agent_run_repl(Agent *agent);
char *agent_process_command(Agent *agent, const char *cmd_name, const char *cmd_args);

#endif
