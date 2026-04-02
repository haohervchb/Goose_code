#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include "config.h"

char *mcp_list_resources(const GooseConfig *cfg, const char *server_name);
char *mcp_read_resource(const GooseConfig *cfg, const char *server_name, const char *uri);

#endif
