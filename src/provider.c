#include "config.h"
#include "util/http.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ProviderProfile PROVIDERS[] = {
    {"openai", "OpenAI", "https://api.openai.com/v1", "gpt-4o", 1, 1},
    {"ollama", "Ollama", "http://localhost:11434/v1", "llama3", 0, 1},
    {"vllm", "vLLM", "http://localhost:8000/v1", "model", 0, 1},
    {"llama.cpp", "llama.cpp", "http://localhost:8080/v1", "model", 0, 1},
    {"ik-llama", "ik-llama", "http://localhost:8080/v1", "model", 0, 1},
};

static const size_t PROVIDER_COUNT = sizeof(PROVIDERS) / sizeof(PROVIDERS[0]);

size_t provider_profile_count(void) {
    return PROVIDER_COUNT;
}

const ProviderProfile *provider_profile_at(size_t index) {
    if (index >= PROVIDER_COUNT) return NULL;
    return &PROVIDERS[index];
}

static char *provider_models_url(const GooseConfig *cfg) {
    size_t len = strlen(cfg->base_url) + 16;
    char *url = malloc(len);
    if (cfg->base_url[strlen(cfg->base_url) - 1] == '/') {
        snprintf(url, len, "%smodels", cfg->base_url);
    } else {
        snprintf(url, len, "%s/models", cfg->base_url);
    }
    return url;
}

const ProviderProfile *provider_profile_find(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < PROVIDER_COUNT; i++) {
        if (strcmp(PROVIDERS[i].name, name) == 0) return &PROVIDERS[i];
    }
    return NULL;
}

const ProviderProfile *provider_profile_detect(const GooseConfig *cfg) {
    if (cfg && cfg->provider) {
        const ProviderProfile *profile = provider_profile_find(cfg->provider);
        if (profile) return profile;
    }
    if (cfg && cfg->base_url) {
        if (strstr(cfg->base_url, "api.openai.com")) return provider_profile_find("openai");
        if (strstr(cfg->base_url, "11434")) return provider_profile_find("ollama");
        if (strstr(cfg->base_url, "8000")) return provider_profile_find("vllm");
        if (strstr(cfg->base_url, "8080")) return provider_profile_find("llama.cpp");
    }
    return provider_profile_find("openai");
}

int provider_apply_preset(GooseConfig *cfg, const char *name, int update_model) {
    const ProviderProfile *next = provider_profile_find(name);
    if (!next) return -1;

    const ProviderProfile *current = provider_profile_detect(cfg);
    int should_update_model = update_model;
    if (!should_update_model && cfg->model && current && strcmp(cfg->model, current->default_model) == 0) {
        should_update_model = 1;
    }
    if (!cfg->model || !cfg->model[0]) should_update_model = 1;

    free(cfg->provider);
    cfg->provider = strdup(next->name);
    free(cfg->base_url);
    cfg->base_url = strdup(next->default_base_url);
    if (should_update_model) {
        free(cfg->model);
        cfg->model = strdup(next->default_model);
    }
    return 0;
}

int provider_requires_api_key(const GooseConfig *cfg) {
    const ProviderProfile *profile = provider_profile_detect(cfg);
    return profile ? profile->requires_api_key : 0;
}

char *provider_list_models(const GooseConfig *cfg) {
    const ProviderProfile *profile = provider_profile_detect(cfg);
    if (!profile || !profile->supports_model_list) {
        return strdup("Model listing is not supported for the current provider.\n");
    }
    if (profile->requires_api_key && (!cfg->api_key || !cfg->api_key[0])) {
        return strdup("Error: this provider requires an API key before listing models.\n");
    }

    char *url = provider_models_url(cfg);
    HttpResponse resp = http_get(url, cfg->api_key);
    free(url);
    if (resp.error) {
        StrBuf out = strbuf_from("Error: ");
        strbuf_append(&out, resp.error);
        strbuf_append(&out, "\n");
        http_response_free(&resp);
        return strbuf_detach(&out);
    }
    if (resp.status_code != 200) {
        StrBuf out = strbuf_new();
        strbuf_append_fmt(&out, "Error: model list request failed with HTTP %ld\n", resp.status_code);
        http_response_free(&resp);
        return strbuf_detach(&out);
    }

    cJSON *json = cJSON_Parse(resp.body.data);
    if (!json) {
        http_response_free(&resp);
        return strdup("Error: failed to parse model list response.\n");
    }

    cJSON *data = json_get_array(json, "data");
    StrBuf out = strbuf_from("Available models:\n");
    if (!data || cJSON_GetArraySize(data) == 0) {
        strbuf_append(&out, "(none returned)\n");
    } else {
        cJSON *item;
        cJSON_ArrayForEach(item, data) {
            const char *id = json_get_string(item, "id");
            if (id) strbuf_append_fmt(&out, "- %s\n", id);
        }
    }
    cJSON_Delete(json);
    http_response_free(&resp);
    return strbuf_detach(&out);
}

char *provider_test_connection(const GooseConfig *cfg) {
    const ProviderProfile *profile = provider_profile_detect(cfg);
    if (profile->requires_api_key && (!cfg->api_key || !cfg->api_key[0])) {
        return strdup("Error: API key is required for this provider but is not configured.\n");
    }

    char *url = provider_models_url(cfg);
    HttpResponse resp = http_get(url, cfg->api_key);
    free(url);

    StrBuf out = strbuf_from("Provider test:\n");
    strbuf_append_fmt(&out, "provider: %s\n", profile ? profile->name : "unknown");
    strbuf_append_fmt(&out, "base_url: %s\n", cfg->base_url ? cfg->base_url : "(none)");
    strbuf_append_fmt(&out, "model: %s\n", cfg->model ? cfg->model : "(none)");
    strbuf_append_fmt(&out, "api_key: %s\n", (cfg->api_key && cfg->api_key[0]) ? "configured" : "not configured");

    if (resp.error) {
        strbuf_append_fmt(&out, "result: failed\nerror: %s\n", resp.error);
        http_response_free(&resp);
        return strbuf_detach(&out);
    }

    strbuf_append_fmt(&out, "result: HTTP %ld\n", resp.status_code);
    if (resp.status_code == 200) strbuf_append(&out, "status: connection ok\n");
    else strbuf_append(&out, "status: connection failed\n");

    http_response_free(&resp);
    return strbuf_detach(&out);
}
