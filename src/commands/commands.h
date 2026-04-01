#ifndef COMMANDS_H
#define COMMANDS_H

#include "config.h"
#include "session.h"
#include "util/cJSON.h"

typedef struct {
    char *name;
    char *description;
    int takes_args;
    char *(*execute)(const char *args, const GooseConfig *cfg, Session *sess);
} Command;

typedef struct {
    Command **commands;
    int count;
    int cap;
} CommandRegistry;

CommandRegistry command_registry_init(void);
void command_registry_register(CommandRegistry *reg, Command cmd);
void command_registry_free(CommandRegistry *reg);
Command *command_registry_find(const CommandRegistry *reg, const char *name);
char *command_registry_execute(CommandRegistry *reg, const char *name, const char *args,
                                const GooseConfig *cfg, Session *sess);
void command_registry_register_all(CommandRegistry *reg);

#endif
