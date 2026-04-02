#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>

#define TERM_RESET     "\033[0m"
#define TERM_BOLD      "\033[1m"
#define TERM_DIM       "\033[2m"
#define TERM_ITALIC    "\033[3m"
#define TERM_UNDERLINE "\033[4m"
#define TERM_RED       "\033[31m"
#define TERM_GREEN     "\033[32m"
#define TERM_YELLOW    "\033[33m"
#define TERM_BLUE      "\033[34m"
#define TERM_MAGENTA   "\033[35m"
#define TERM_CYAN      "\033[36m"
#define TERM_WHITE     "\033[37m"

void term_init(void);
void term_restore(void);
int term_get_size(int *rows, int *cols);
char *term_read_line(const char *prompt);
void term_clear_screen(void);
void term_print_colored(const char *text, const char *color);
char *term_format_prompt(const char *working_dir, int plan_mode);
void term_print_block_header(const char *label, const char *color);
void term_print_tool_call(const char *name, const char *args);
void term_print_tool_result(const char *name, int is_error);
void term_print_banner(void);

#endif
