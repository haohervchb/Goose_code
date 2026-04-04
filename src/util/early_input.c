#include "util/early_input.h"
#include "util/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static char *g_early_input_buffer = NULL;
static size_t g_early_input_len = 0;
static size_t g_early_input_cap = 0;
static pthread_mutex_t g_early_input_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_initialized = 0;
static int g_capturing = 0;

void early_input_capture_start(void) {
    pthread_mutex_lock(&g_early_input_mutex);
    g_capturing = 1;
    pthread_mutex_unlock(&g_early_input_mutex);
}

void early_input_capture_stop(void) {
    pthread_mutex_lock(&g_early_input_mutex);
    g_capturing = 0;
    pthread_mutex_unlock(&g_early_input_mutex);
}

void early_input_init(void) {
    pthread_mutex_lock(&g_early_input_mutex);
    if (g_early_input_buffer) {
        free(g_early_input_buffer);
    }
    g_early_input_buffer = NULL;
    g_early_input_len = 0;
    g_early_input_cap = 0;
    g_initialized = 1;
    pthread_mutex_unlock(&g_early_input_mutex);
}

void early_input_capture(const char *input, size_t len) {
    if (!input || len == 0) return;
    
    pthread_mutex_lock(&g_early_input_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_early_input_mutex);
        return;
    }
    
    size_t needed = g_early_input_len + len + 1;
    if (needed > g_early_input_cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(g_early_input_buffer, new_cap);
        if (!new_buf) {
            pthread_mutex_unlock(&g_early_input_mutex);
            return;
        }
        g_early_input_buffer = new_buf;
        g_early_input_cap = new_cap;
    }
    
    memcpy(g_early_input_buffer + g_early_input_len, input, len);
    g_early_input_len += len;
    g_early_input_buffer[g_early_input_len] = '\0';
    
    pthread_mutex_unlock(&g_early_input_mutex);
}

char *early_input_consume(void) {
    pthread_mutex_lock(&g_early_input_mutex);
    
    if (!g_early_input_buffer || g_early_input_len == 0) {
        pthread_mutex_unlock(&g_early_input_mutex);
        return NULL;
    }
    
    char *result = strdup(g_early_input_buffer);
    g_early_input_buffer[0] = '\0';
    g_early_input_len = 0;
    
    pthread_mutex_unlock(&g_early_input_mutex);
    return result;
}

int early_input_has_pending(void) {
    pthread_mutex_lock(&g_early_input_mutex);
    int has_pending = g_initialized && g_early_input_buffer && g_early_input_len > 0;
    pthread_mutex_unlock(&g_early_input_mutex);
    return has_pending;
}
