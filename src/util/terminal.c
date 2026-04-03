#include "util/terminal.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <ctype.h>

static struct termios orig_termios;
static int term_saved = 0;
static char **term_history = NULL;
static size_t term_history_count = 0;
static size_t term_history_cap = 0;

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

static size_t term_visible_width(const char *text) {
    size_t width = 0;
    for (size_t i = 0; text && text[i]; i++) {
        if (text[i] == '\033' && text[i + 1] == '[') {
            i += 2;
            while (text[i] && !((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z'))) {
                i++;
            }
            continue;
        }
        width++;
    }
    return width;
}

static void term_buffer_reserve(TermInputBuffer *buf, size_t needed) {
    if (needed <= buf->cap) return;
    size_t new_cap = buf->cap ? buf->cap : 64;
    while (new_cap < needed) new_cap *= 2;
    buf->text = realloc(buf->text, new_cap);
    buf->cap = new_cap;
}

static void term_history_push(const char *text) {
    if (!text || !text[0]) return;
    int only_space = 1;
    for (const char *p = text; *p; p++) {
        if (!isspace((unsigned char)*p)) {
            only_space = 0;
            break;
        }
    }
    if (only_space) return;
    if (term_history_count > 0 && strcmp(term_history[term_history_count - 1], text) == 0) return;

    if (term_history_count + 1 > term_history_cap) {
        size_t new_cap = term_history_cap ? term_history_cap * 2 : 32;
        term_history = realloc(term_history, new_cap * sizeof(char *));
        term_history_cap = new_cap;
    }
    term_history[term_history_count++] = strdup(text);
}

void term_buffer_init(TermInputBuffer *buf) {
    memset(buf, 0, sizeof(*buf));
    term_buffer_reserve(buf, 1);
    buf->text[0] = '\0';
}

void term_buffer_free(TermInputBuffer *buf) {
    free(buf->text);
    memset(buf, 0, sizeof(*buf));
}

void term_buffer_set(TermInputBuffer *buf, const char *text) {
    size_t len = text ? strlen(text) : 0;
    term_buffer_reserve(buf, len + 1);
    if (len) memcpy(buf->text, text, len);
    buf->text[len] = '\0';
    buf->len = len;
    buf->cursor = len;
}

void term_buffer_insert_char(TermInputBuffer *buf, char ch) {
    term_buffer_reserve(buf, buf->len + 2);
    memmove(buf->text + buf->cursor + 1, buf->text + buf->cursor, buf->len - buf->cursor + 1);
    buf->text[buf->cursor] = ch;
    buf->cursor++;
    buf->len++;
}

void term_buffer_backspace(TermInputBuffer *buf) {
    if (buf->cursor == 0) return;
    memmove(buf->text + buf->cursor - 1, buf->text + buf->cursor, buf->len - buf->cursor + 1);
    buf->cursor--;
    buf->len--;
}

void term_buffer_delete(TermInputBuffer *buf) {
    if (buf->cursor >= buf->len) return;
    memmove(buf->text + buf->cursor, buf->text + buf->cursor + 1, buf->len - buf->cursor);
    buf->len--;
}

void term_buffer_move_left(TermInputBuffer *buf) {
    if (buf->cursor > 0) buf->cursor--;
}

void term_buffer_move_right(TermInputBuffer *buf) {
    if (buf->cursor < buf->len) buf->cursor++;
}

void term_buffer_move_home(TermInputBuffer *buf) {
    buf->cursor = 0;
}

void term_buffer_move_end(TermInputBuffer *buf) {
    buf->cursor = buf->len;
}

static size_t term_count_lines(const char *text) {
    size_t lines = 1;
    for (const char *p = text; p && *p; p++) {
        if (*p == '\n') lines++;
    }
    return lines;
}

static void term_render_input(const char *prompt, const char *continuation_prompt,
                              const TermInputBuffer *buf, size_t previous_lines) {
    size_t cursor_row = 0;
    size_t cursor_col = 0;
    size_t prompt_len = term_visible_width(prompt ? prompt : "");
    size_t cont_len = term_visible_width(continuation_prompt ? continuation_prompt : "");

    if (previous_lines > 1) {
        printf("\033[%zuA", previous_lines - 1);
    }
    printf("\r\033[J");

    const char *text = buf->text ? buf->text : "";
    const char *line_start = text;
    size_t line_index = 0;
    while (1) {
        const char *newline = strchr(line_start, '\n');
        const char *line_end = newline ? newline : line_start + strlen(line_start);
        printf("%s", line_index == 0 ? (prompt ? prompt : "") : (continuation_prompt ? continuation_prompt : ""));
        fwrite(line_start, 1, (size_t)(line_end - line_start), stdout);
        if (!newline) break;
        putchar('\n');
        line_index++;
        line_start = newline + 1;
    }

    for (size_t i = 0; i < buf->cursor; i++) {
        if (buf->text[i] == '\n') {
            cursor_row++;
            cursor_col = 0;
        } else {
            cursor_col++;
        }
    }

    size_t rendered_lines = term_count_lines(buf->text);
    if (rendered_lines > 0 && rendered_lines - 1 > cursor_row) {
        printf("\033[%zuA", (rendered_lines - 1) - cursor_row);
    }
    printf("\r");
    size_t target_col = (cursor_row == 0 ? prompt_len : cont_len) + cursor_col;
    if (target_col > 0) printf("\033[%zuC", target_col);
    fflush(stdout);
}

static int term_read_escape_sequence(char *buf, size_t buf_size) {
    size_t n = 0;
    while (n + 1 < buf_size) {
        ssize_t r = read(STDIN_FILENO, &buf[n], 1);
        if (r != 1) break;
        n++;
        if ((buf[0] == '[' && ((buf[n - 1] >= 'A' && buf[n - 1] <= 'Z') || buf[n - 1] == '~')) ||
            (buf[0] == 'O' && (buf[n - 1] >= 'A' && buf[n - 1] <= 'Z'))) {
            break;
        }
    }
    buf[n] = '\0';
    return (int)n;
}

void term_init(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    term_saved = 1;
    raw = orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON);
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

char *term_read_line_opts(const char *prompt, int multiline, int history_enabled) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        if (prompt) fprintf(stdout, "%s", prompt);
        fflush(stdout);
        char *buf = NULL;
        size_t cap = 0;
        ssize_t n = getline(&buf, &cap, stdin);
        if (n <= 0) {
            free(buf);
            return NULL;
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        return buf;
    }

    const char *continuation = "... ";
    TermInputBuffer input;
    term_buffer_init(&input);
    int history_index = -1;
    char *draft = NULL;
    size_t previous_lines = 1;

    term_init();
    term_render_input(prompt ? prompt : "", continuation, &input, previous_lines);
    previous_lines = term_count_lines(input.text);

    while (1) {
        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r != 1) {
            term_restore();
            term_buffer_free(&input);
            free(draft);
            return NULL;
        }

        if (c == 3) {
            term_restore();
            term_buffer_free(&input);
            free(draft);
            return NULL;
        }

        if (c == '\r') {
            term_restore();
            putchar('\n');
            fflush(stdout);
            char *result = strdup(input.text);
            if (history_enabled) term_history_push(result);
            term_buffer_free(&input);
            free(draft);
            return result;
        }

        if (c == '\n') {
            if (multiline) {
                term_buffer_insert_char(&input, '\n');
            } else {
                term_restore();
                putchar('\n');
                fflush(stdout);
                char *result = strdup(input.text);
                term_buffer_free(&input);
                free(draft);
                return result;
            }
        } else if (c == 127 || c == 8) {
            term_buffer_backspace(&input);
            history_index = -1;
        } else if (c == 4) {
            if (input.len == 0) {
                term_restore();
                term_buffer_free(&input);
                free(draft);
                return NULL;
            }
            term_buffer_delete(&input);
            history_index = -1;
        } else if (c == 1) {
            term_buffer_move_home(&input);
        } else if (c == 5) {
            term_buffer_move_end(&input);
        } else if (c == 27) {
            char seq[16] = {0};
            term_read_escape_sequence(seq, sizeof(seq));
            if (strcmp(seq, "[D") == 0) {
                term_buffer_move_left(&input);
            } else if (strcmp(seq, "[C") == 0) {
                term_buffer_move_right(&input);
            } else if (strcmp(seq, "[H") == 0 || strcmp(seq, "OH") == 0 || strcmp(seq, "[1~") == 0) {
                term_buffer_move_home(&input);
            } else if (strcmp(seq, "[F") == 0 || strcmp(seq, "OF") == 0 || strcmp(seq, "[4~") == 0) {
                term_buffer_move_end(&input);
            } else if (strcmp(seq, "[3~") == 0) {
                term_buffer_delete(&input);
                history_index = -1;
            } else if (history_enabled && strcmp(seq, "[A") == 0) {
                if (term_history_count > 0) {
                    if (history_index < 0) {
                        free(draft);
                        draft = strdup(input.text);
                        history_index = (int)term_history_count - 1;
                    } else if (history_index > 0) {
                        history_index--;
                    }
                    term_buffer_set(&input, term_history[history_index]);
                }
            } else if (history_enabled && strcmp(seq, "[B") == 0) {
                if (history_index >= 0) {
                    if (history_index + 1 < (int)term_history_count) {
                        history_index++;
                        term_buffer_set(&input, term_history[history_index]);
                    } else {
                        history_index = -1;
                        term_buffer_set(&input, draft ? draft : "");
                    }
                }
            }
        } else if (c >= 32 && c < 127) {
            term_buffer_insert_char(&input, c);
            history_index = -1;
        }

        term_render_input(prompt ? prompt : "", continuation, &input, previous_lines);
        previous_lines = term_count_lines(input.text);
    }
}

char *term_read_line(const char *prompt) {
    return term_read_line_opts(prompt, 0, 0);
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
