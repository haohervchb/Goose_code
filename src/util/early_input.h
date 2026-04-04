#ifndef EARLY_INPUT_H
#define EARLY_INPUT_H

#include <stddef.h>

void early_input_init(void);
void early_input_capture(const char *input, size_t len);
char *early_input_consume(void);
int early_input_has_pending(void);

#endif
