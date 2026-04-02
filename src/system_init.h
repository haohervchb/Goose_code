#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "agent.h"

cJSON *system_init_build_metadata(const Agent *agent);
char *system_init_render_metadata(const Agent *agent);

#endif
