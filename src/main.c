#include "agent.h"
#include "util/terminal.h"
#include "util/strbuf.h"
#include "util/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

static Agent *g_agent = NULL;

static void signal_handler(int sig) {
    (void)sig;
    if (g_agent) {
        g_agent->running = 0;
        printf("\n" TERM_YELLOW "Interrupted. Saving session..." TERM_RESET "\n");
    }
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options] [prompt]\n", prog);
    printf("\nOptions:\n");
    printf("  --model <model>       Set the model name\n");
    printf("  --base-url <url>      Set the API base URL\n");
    printf("  --permission <mode>   Set permission mode (read-only, workspace-write, danger-full-access, prompt, allow)\n");
    printf("  --max-turns <n>       Set max tool-use turns per message\n");
    printf("  --session <id>        Resume a saved session\n");
    printf("  --help                Show this help\n");
    printf("\nEnvironment variables:\n");
    printf("  OPENAI_API_KEY        API key (optional for local servers)\n");
    printf("  OPENAI_BASE_URL       API base URL (default: https://api.openai.com/v1)\n");
    printf("  OPENAI_MODEL          Model name (default: gpt-4o)\n");
    printf("  GOOSECODE_PERMS       Default permission mode\n");
    printf("  GOOSECODE_MAX_TURNS   Max turns per message\n");
    printf("\nExamples:\n");
    printf("  %s                          Start interactive REPL\n", prog);
    printf("  %s \"explain this code\"      Single-turn query\n", prog);
    printf("  %s --model gpt-4o-mini      Use a different model\n", prog);
}

static char *read_line(const char *prompt_text, const char *default_val) {
    printf("%s", prompt_text);
    if (default_val) printf(" [%s]", default_val);
    printf(": ");
    fflush(stdout);
    char *buf = NULL;
    size_t cap = 0;
    ssize_t n = getline(&buf, &cap, stdin);
    if (n <= 0) {
        free(buf);
        return default_val ? strdup(default_val) : NULL;
    }
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) {
        buf[--n] = '\0';
    }
    if (n == 0 && default_val) {
        free(buf);
        return strdup(default_val);
    }
    return buf;
}

static void interactive_setup(Agent *agent) {
    printf("\n" TERM_BOLD "=== goosecode setup ===" TERM_RESET "\n\n");

    char *home = config_get_home_dir();
    char settings_path[1024];
    snprintf(settings_path, sizeof(settings_path), "%s/.goosecode", home);
    mkdir(settings_path, 0755);
    snprintf(settings_path, sizeof(settings_path), "%s/.goosecode/settings.json", home);

    const char *cur_base = agent->config.base_url;
    const char *cur_model = agent->config.model;

    char *base_url = read_line("API base URL", cur_base ? cur_base : "https://api.openai.com/v1");
    char *model = read_line("Model name", cur_model ? cur_model : "gpt-4o");
    char *api_key = read_line("API key (leave empty for local servers)", agent->config.api_key ? "(hidden)" : "");

    cJSON *settings = cJSON_CreateObject();
    cJSON_AddStringToObject(settings, "base_url", base_url);
    cJSON_AddStringToObject(settings, "model", model);
    if (api_key && strlen(api_key) > 0) {
        cJSON_AddStringToObject(settings, "api_key", api_key);
    }

    FILE *f = fopen(settings_path, "w");
    if (f) {
        char *json = cJSON_Print(settings);
        fprintf(f, "%s\n", json);
        fclose(f);
        free(json);
        printf("\n" TERM_GREEN "Settings saved to %s" TERM_RESET "\n\n", settings_path);
    }

    if (base_url && (!agent->config.base_url || strcmp(base_url, agent->config.base_url) != 0)) {
        free(agent->config.base_url);
        agent->config.base_url = strdup(base_url);
        agent->api_cfg.base_url = agent->config.base_url;
    }
    if (model && (!agent->config.model || strcmp(model, agent->config.model) != 0)) {
        free(agent->config.model);
        agent->config.model = strdup(model);
        agent->api_cfg.model = agent->config.model;
    }
    if (api_key && strlen(api_key) > 0) {
        if (agent->config.api_key) free(agent->config.api_key);
        agent->config.api_key = strdup(api_key);
    }

    cJSON_Delete(settings);
    free(home);
    free(base_url);
    free(model);
    free(api_key);
}

int main(int argc, char *argv[]) {
    const char *model_override = NULL;
    const char *base_url_override = NULL;
    const char *perm_override = NULL;
    const char *session_id = NULL;
    int max_turns_override = 0;
    const char *prompt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_override = argv[++i];
        } else if (strcmp(argv[i], "--base-url") == 0 && i + 1 < argc) {
            base_url_override = argv[++i];
        } else if (strcmp(argv[i], "--permission") == 0 && i + 1 < argc) {
            perm_override = argv[++i];
        } else if (strcmp(argv[i], "--max-turns") == 0 && i + 1 < argc) {
            max_turns_override = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc) {
            session_id = argv[++i];
        } else if (argv[i][0] != '-') {
            prompt = argv[i];
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_agent = agent_init(NULL);

    if (model_override) {
        free(g_agent->config.model);
        g_agent->config.model = strdup(model_override);
        g_agent->api_cfg.model = g_agent->config.model;
    }
    if (base_url_override) {
        free(g_agent->config.base_url);
        g_agent->config.base_url = strdup(base_url_override);
        g_agent->api_cfg.base_url = g_agent->config.base_url;
    }
    if (perm_override) {
        g_agent->config.permission_mode = config_perm_mode_from_str(perm_override);
    }
    if (max_turns_override > 0) {
        g_agent->config.max_turns = max_turns_override;
    }

    const char *env_base = getenv("OPENAI_BASE_URL");
    if (!env_base && !g_agent->config.base_url) {
        interactive_setup(g_agent);
    }

    if (session_id) {
        Session *loaded = session_load(g_agent->config.session_dir, session_id);
        if (loaded) {
            session_free(g_agent->session);
            g_agent->session = loaded;
            printf("Resumed session: %s (%d messages)\n", session_id, cJSON_GetArraySize(loaded->messages));
        } else {
            fprintf(stderr, "Warning: session '%s' not found, starting new session\n", session_id);
        }
    }

    if (prompt) {
        term_print_block_header("request", TERM_BLUE);
        printf("%s\n", prompt);
        int rc = agent_run_turn(g_agent, prompt);
        printf("\n");
        session_save(g_agent->config.session_dir, g_agent->session);
        agent_free(g_agent);
        g_agent = NULL;
        return rc == 0 ? 0 : 1;
    }

    agent_run_repl(g_agent);
    agent_free(g_agent);
    g_agent = NULL;
    return 0;
}
