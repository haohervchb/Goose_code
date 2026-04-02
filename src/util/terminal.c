#include "util/terminal.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static struct termios orig_termios;
static int term_saved = 0;

static const char *term_basename(const char *path) {
    if (!path || !path[0]) return ".";
    const char *slash = strrchr(path, '/');
    if (!slash || !slash[1]) return path;
    return slash + 1;
}

static void term_print_rule(size_t width) {
    for (size_t i = 0; i < width; i++) putchar('-');
    putchar('\n');
}

void term_init(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    term_saved = 1;
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void term_restore(void) {
    if (term_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        term_saved = 0;
    }
}

int term_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return -1;
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
    return 0;
}

char *term_read_line(const char *prompt) {
    if (prompt) fprintf(stdout, "%s", prompt);
    fflush(stdout);

    StrBuf sb = strbuf_new();
    char c;
    int got_input = 0;
    while (1) {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r != 1) {
            if (got_input) break;
            strbuf_free(&sb);
            return NULL;
        }
        if (c == '\n' || c == '\r') break;
        if (c == 3) { strbuf_free(&sb); return NULL; }
        if (c == 127 || c == 8) {
            if (sb.len > 0) {
                sb.len--;
                sb.data[sb.len] = '\0';
                fprintf(stdout, "\b \b");
                fflush(stdout);
            }
        } else if (c >= 32 && c < 127) {
            strbuf_append_char(&sb, c);
            fprintf(stdout, "%c", c);
            fflush(stdout);
            got_input = 1;
        }
    }
    fprintf(stdout, "\n");
    fflush(stdout);
    return strbuf_detach(&sb);
}

void term_clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void term_print_colored(const char *text, const char *color) {
    printf("%s%s%s", color, text, TERM_RESET);
}

char *term_format_prompt(const char *working_dir, int plan_mode) {
    StrBuf out = strbuf_new();
    const char *repo = term_basename(working_dir);

    strbuf_append(&out, TERM_BOLD "goosecode" TERM_RESET);
    strbuf_append(&out, TERM_DIM "[" TERM_RESET);
    strbuf_append(&out, TERM_CYAN);
    strbuf_append(&out, repo && repo[0] ? repo : ".");
    strbuf_append(&out, TERM_RESET TERM_DIM "]" TERM_RESET);
    if (plan_mode) {
        strbuf_append(&out, TERM_DIM "[" TERM_RESET TERM_YELLOW "plan" TERM_RESET TERM_DIM "]" TERM_RESET);
    }
    strbuf_append(&out, TERM_BOLD "> " TERM_RESET);
    return strbuf_detach(&out);
}

void term_print_block_header(const char *label, const char *color) {
    if (!label) return;
    printf("\n%s%s%s\n", color ? color : "", label, TERM_RESET);
    if (color) printf("%s", color);
    term_print_rule(strlen(label));
    if (color) printf("%s", TERM_RESET);
    fflush(stdout);
}

void term_print_tool_call(const char *name, const char *args) {
    printf("\n" TERM_CYAN "[tool] %s" TERM_RESET "\n", name ? name : "unknown");
    if (args && args[0]) {
        printf(TERM_DIM "  %s\n" TERM_RESET, args);
    }
    fflush(stdout);
}

void term_print_tool_result(const char *name, int is_error) {
    printf("\n%s[result] %s%s%s\n",
           is_error ? TERM_RED : TERM_CYAN,
           name ? name : "unknown",
           is_error ? " (error)" : "",
           TERM_RESET);
    fflush(stdout);
}

void term_print_banner(void) {
    printf(TERM_BOLD TERM_GREEN);
    printf(" __      \n");
    printf("___( o)>    GOOSE CODE\n");
    printf("\\ <_. )     High-Performance Inference\n");
    printf(" `---'      v0.1.0\n");
    printf(TERM_RESET);
    printf(TERM_DIM "  Type /help for commands. /exit to quit.\n\n" TERM_RESET);
    fflush(stdout);
}
