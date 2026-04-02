#include "commands/commands.h"
#include "system_init.h"
#include <stdlib.h>
#include <string.h>

static char *cmd_runtime_exec(const char *args, const GooseConfig *cfg, Session *sess) {
    (void)args; (void)cfg; (void)sess;
    Agent *agent = agent_current();
    if (!agent) return strdup("Runtime metadata unavailable.\n");
    return system_init_render_metadata(agent);
}

void cmd_runtime_register(CommandRegistry *reg) {
    Command cmd = {strdup("runtime"), strdup("Show structured runtime metadata"), 0, cmd_runtime_exec};
    command_registry_register(reg, cmd);
}
