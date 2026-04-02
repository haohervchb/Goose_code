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
