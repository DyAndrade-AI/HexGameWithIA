#ifndef UI_H
#define UI_H

#include <stddef.h>

int ui_init(void);
void ui_shutdown(void);
void ui_draw_board(const char *board, int size, const char *message);
int ui_prompt(const char *prompt, char *buffer, size_t len);
int ui_wait_move(int size, int *pos_out);

#endif
