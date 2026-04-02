#include "commands/commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CommandRegistry command_registry_init(void) {
    CommandRegistry reg = {0};
    reg.cap = 16;
    reg.commands = calloc(reg.cap, sizeof(Command *));
    return reg;
}

void command_registry_register(CommandRegistry *reg, Command cmd) {
    if (reg->count >= reg->cap) {
        reg->cap *= 2;
        reg->commands = realloc(reg->commands, reg->cap * sizeof(Command *));
    }
    reg->commands[reg->count] = malloc(sizeof(Command));
    *reg->commands[reg->count] = cmd;
    reg->count++;
}

void command_registry_free(CommandRegistry *reg) {
    for (int i = 0; i < reg->count; i++) {
        if (reg->commands[i]) {
            free(reg->commands[i]->name);
            free(reg->commands[i]->description);
            free(reg->commands[i]);
        }
    }
    free(reg->commands);
    reg->commands = NULL;
    reg->count = 0;
    reg->cap = 0;
}

Command *command_registry_find(const CommandRegistry *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->commands[i]->name, name) == 0) return reg->commands[i];
    }
    return NULL;
}

char *command_registry_execute(CommandRegistry *reg, const char *name, const char *args,
                                const GooseConfig *cfg, Session *sess) {
    Command *cmd = command_registry_find(reg, name);
    if (!cmd) return strdup("Error: command not found");
    return cmd->execute(args, cfg, sess);
}

void command_registry_register_all(CommandRegistry *reg);

void command_registry_register_all(CommandRegistry *reg) {
    extern void cmd_help_register(CommandRegistry *);
    extern void cmd_model_register(CommandRegistry *);
    extern void cmd_session_register(CommandRegistry *);
    extern void cmd_compact_register(CommandRegistry *);
    extern void cmd_permissions_register(CommandRegistry *);
    extern void cmd_clear_register(CommandRegistry *);
    extern void cmd_cost_register(CommandRegistry *);
    extern void cmd_exit_register(CommandRegistry *);
    extern void cmd_plan_register(CommandRegistry *);
    extern void cmd_config_register(CommandRegistry *);
    extern void cmd_branch_register(CommandRegistry *);
    extern void cmd_commit_register(CommandRegistry *);
    extern void cmd_review_register(CommandRegistry *);
    extern void cmd_tasks_register(CommandRegistry *);
    extern void cmd_tools_register(CommandRegistry *);

    cmd_help_register(reg);
    cmd_model_register(reg);
    cmd_session_register(reg);
    cmd_compact_register(reg);
    cmd_permissions_register(reg);
    cmd_clear_register(reg);
    cmd_cost_register(reg);
    cmd_exit_register(reg);
    cmd_plan_register(reg);
    cmd_config_register(reg);
    cmd_branch_register(reg);
    cmd_commit_register(reg);
    cmd_review_register(reg);
    cmd_tasks_register(reg);
    cmd_tools_register(reg);
}
