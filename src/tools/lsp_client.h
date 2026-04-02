#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include "config.h"

char *lsp_execute_request(const GooseConfig *cfg, const char *args_json);

#endif
