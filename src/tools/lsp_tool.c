#include "tools/tools.h"
#include "tools/lsp_client.h"

char *tool_execute_lsp(const char *args, const GooseConfig *cfg) {
    return lsp_execute_request(cfg, args);
}
