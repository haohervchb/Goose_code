#ifndef API_H
#define API_H

#include "util/cJSON.h"
#include "util/strbuf.h"
#include <stddef.h>

typedef enum {
    API_OK,
    API_ERROR_NETWORK,
    API_ERROR_AUTH,
    API_ERROR_RATE_LIMIT,
    API_ERROR_INTERRUPTED,
    API_ERROR_SERVER
} ApiStatus;

typedef struct {
    ApiStatus status;
    char *error;
    StrBuf text_content;
    cJSON *tool_calls;
    long input_tokens;
    long output_tokens;
    long cache_read_tokens;
    long cache_creation_tokens;
    int finish_reason_stop;
    int finish_reason_tool_calls;
    char *system_fingerprint;
    char *model;
} ApiResponse;

typedef struct {
    const char *base_url;
    const char *api_key;
    const char *model;
    int max_tokens;
    double temperature;
    int max_retries;
} ApiConfig;

typedef struct {
    void (*on_text)(const char *text, size_t len, void *ctx);
    void (*on_tool_call)(const char *id, const char *name, const char *args, void *ctx);
    void (*on_done)(void *ctx);
    void *ctx;
    volatile int *abort_flag;
} ApiStreamCallbacks;

ApiConfig api_config_default(void);
void api_config_free(ApiConfig *cfg);

ApiStatus api_chat_completions(const ApiConfig *cfg, const cJSON *messages, const cJSON *tools,
                               int stream, ApiStreamCallbacks *callbacks, ApiResponse *resp);

ApiResponse api_send_message(const ApiConfig *cfg, const cJSON *messages, const cJSON *tools);
void api_response_free(ApiResponse *resp);
const char *api_status_str(ApiStatus s);

// Get model context window from API
int api_get_model_context_window(const ApiConfig *cfg, const char *model);

#endif
