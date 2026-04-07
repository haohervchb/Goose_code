#include "agent.h"
#include "session_memory.h"
#include "util/terminal.h"
#include "util/strbuf.h"
#include "util/cJSON.h"
#include "util/early_input.h"
#include "util/tui_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

static Agent *g_agent = NULL;

static int run_tui_mode(int argc, char *argv[]);

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

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
    printf("  --provider <name>    Set provider preset (openai, ollama, vllm, llama.cpp, ik-llama)\n");
    printf("  --model <model>       Set the model name\n");
    printf("  --base-url <url>      Set the API base URL\n");
    printf("  --permission <mode>   Set permission mode (read-only, workspace-write, danger-full-access, prompt, allow)\n");
    printf("  --max-turns <n>       Set max tool-use turns per message\n");
    printf("  --session <id>        Resume a saved session\n");
    printf("  --repl                Use legacy REPL instead of TUI (default: TUI)\n");
    printf("  --help                Show this help\n");
    printf("\nEnvironment variables:\n");
    printf("  OPENAI_API_KEY        API key (optional for local servers)\n");
    printf("  OPENAI_BASE_URL       API base URL (default: https://api.openai.com/v1)\n");
    printf("  OPENAI_MODEL          Model name (default: gpt-4o)\n");
    printf("  GOOSECODE_PROVIDER    Provider preset\n");
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

    const ProviderProfile *cur_provider = provider_profile_detect(&agent->config);
    const char *cur_base = agent->config.base_url;
    const char *cur_model = agent->config.model;
    const char *provider_default = cur_provider ? cur_provider->name : "openai";

    printf("Available providers:\n");
    for (size_t i = 0; i < provider_profile_count(); i++) {
        const ProviderProfile *p = provider_profile_at(i);
        printf("  - %s (%s)\n", p->name, p->default_base_url);
    }

    char *provider = read_line("Provider preset", provider_default);
    if (provider_apply_preset(&agent->config, provider, 1) != 0) {
        free(provider);
        provider = strdup(provider_default);
        provider_apply_preset(&agent->config, provider, 1);
    }

    char *base_url = read_line("API base URL", cur_base ? cur_base : agent->config.base_url);
    char *model = read_line("Model name", cur_model ? cur_model : agent->config.model);
    char *api_key = NULL;
    if (provider_requires_api_key(&agent->config)) {
        api_key = read_line("API key", agent->config.api_key ? "(hidden)" : "");
    } else {
        api_key = read_line("API key (optional for this provider)", agent->config.api_key ? "(hidden)" : "");
    }

    cJSON *settings = cJSON_CreateObject();
    cJSON_AddStringToObject(settings, "provider", provider);
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

    free(agent->config.provider);
    agent->config.provider = strdup(provider);
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
    free(provider);
    free(base_url);
    free(model);
    free(api_key);
}

static int should_run_interactive_setup(void) {
    if (getenv("OPENAI_BASE_URL") || getenv("OPENAI_MODEL") || getenv("OPENAI_API_KEY") || getenv("GOOSECODE_PROVIDER")) {
        return 0;
    }

    char *home = config_get_home_dir();
    char user_settings[1024];
    snprintf(user_settings, sizeof(user_settings), "%s/.goosecode/settings.json", home);
    free(home);

    if (file_exists(user_settings)) return 0;
    if (file_exists(".goosecode/settings.json")) return 0;
    return 1;
}

int main(int argc, char *argv[]) {
    const char *provider_override = NULL;
    const char *model_override = NULL;
    const char *base_url_override = NULL;
    const char *perm_override = NULL;
    const char *session_id = NULL;
    int use_tui = 1;  // Default to TUI mode
    int max_turns_override = 0;
    const char *prompt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--repl") == 0) {
            use_tui = 0;  // Use legacy REPL instead of TUI
        } else if (strcmp(argv[i], "--tui-mode") == 0) {
            // TUI mode: run as backend for the TUI subprocess
            return run_tui_mode(argc, argv);
        } else if (strcmp(argv[i], "--provider") == 0 && i + 1 < argc) {
            provider_override = argv[++i];
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
    early_input_capture_start();

    if (provider_override) {
        provider_apply_preset(&g_agent->config, provider_override, 1);
        g_agent->api_cfg.base_url = g_agent->config.base_url;
        g_agent->api_cfg.model = g_agent->config.model;
    }

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

    if (should_run_interactive_setup()) {
        interactive_setup(g_agent);
        g_agent->api_cfg.base_url = g_agent->config.base_url;
        g_agent->api_cfg.model = g_agent->config.model;
        g_agent->api_cfg.api_key = g_agent->config.api_key;
    }

    if (session_id) {
        Session *loaded = session_load(g_agent->config.session_dir, session_id);
        if (loaded) {
            session_free(g_agent->session);
            g_agent->session = loaded;
            session_memory_ensure(&g_agent->config, g_agent->session);
            printf("Resumed session: %s (%d messages)\n", session_id, cJSON_GetArraySize(loaded->messages));
        } else {
            fprintf(stderr, "Warning: session '%s' not found, starting new session\n", session_id);
            free(g_agent->session->id);
            g_agent->session->id = strdup(session_id);
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

    if (use_tui) {
        execl("./goosecode-tui", "goosecode-tui", NULL);
        fprintf(stderr, "Failed to launch TUI. Falling back to REPL mode...\n");
    }

    agent_run_repl(g_agent);
    agent_free(g_agent);
    g_agent = NULL;
    early_input_capture_stop();
    return 0;
}

static int run_tui_mode(int argc, char *argv[]) {
    const char *model_override = NULL;
    const char *provider_override = NULL;
    const char *base_url_override = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_override = argv[++i];
        } else if (strcmp(argv[i], "--provider") == 0 && i + 1 < argc) {
            provider_override = argv[++i];
        } else if (strcmp(argv[i], "--base-url") == 0 && i + 1 < argc) {
            base_url_override = argv[++i];
        }
    }
    
    tui_protocol_init();
    
    g_agent = agent_init(NULL);
    
    if (provider_override) {
        provider_apply_preset(&g_agent->config, provider_override, 1);
    }
    if (base_url_override) {
        free(g_agent->config.base_url);
        g_agent->config.base_url = strdup(base_url_override);
    }
    if (model_override) {
        free(g_agent->config.model);
        g_agent->config.model = strdup(model_override);
    }
    g_agent->api_cfg.base_url = g_agent->config.base_url;
    g_agent->api_cfg.model = g_agent->config.model;
    
    // Set up TUI callbacks for streaming output
    agent_set_callbacks(g_agent, 
        tui_on_text,
        tui_on_tool_start,
        tui_on_tool_output,
        tui_on_tool_done,
        NULL);
    
    tui_protocol_send_init_ok(g_agent->session->id, g_agent->config.session_dir);
    
    while (1) {
        TUIRequest req;
        if (tui_protocol_read_request(&req) != 0) {
            break;
        }
        
        switch (req.type) {
            case TUI_MSG_INIT:
                if (req.working_dir) {
                    free(g_agent->config.working_dir);
                    g_agent->config.working_dir = strdup(req.working_dir);
                }
                // Apply provider first (sets defaults), then override with explicit config
                if (req.provider) {
                    provider_apply_preset(&g_agent->config, req.provider, 1);
                }
                if (req.base_url) {
                    free(g_agent->config.base_url);
                    g_agent->config.base_url = strdup(req.base_url);
                    g_agent->api_cfg.base_url = g_agent->config.base_url;
                }
                // Model must be set AFTER provider (so it doesn't get overwritten)
                if (req.model) {
                    free(g_agent->config.model);
                    g_agent->config.model = strdup(req.model);
                    g_agent->api_cfg.model = g_agent->config.model;
                } else if (!model_override && req.provider) {
                    // Use provider's default model if no explicit model was given
                    g_agent->api_cfg.model = g_agent->config.model;
                }
                break;
                
            case TUI_MSG_PROMPT:
                if (req.text) {
                    agent_run_turn(g_agent, req.text);
                    tui_protocol_send_response_chunk("", 1);
                    tui_protocol_send_session_info(
                        cJSON_GetArraySize(g_agent->session->messages),
                        g_agent->session->plan_mode
                    );
                }
                break;
                
            case TUI_MSG_COMMAND:
                if (req.cmd_name) {
                    char *result = command_registry_execute(
                        &g_agent->commands, 
                        req.cmd_name, 
                        req.cmd_args, 
                        &g_agent->config, 
                        g_agent->session
                    );
                    if (result) {
                        tui_protocol_send_response_chunk(result, 1);
                        free(result);
                    }
                }
                break;
                
            case TUI_MSG_QUIT:
                tui_protocol_free_request(&req);
                goto done;
                
            default:
                break;
        }
        
        tui_protocol_free_request(&req);
    }
    
done:
    session_save(g_agent->config.session_dir, g_agent->session);
    agent_free(g_agent);
    g_agent = NULL;
    tui_protocol_cleanup();
    return 0;
}
