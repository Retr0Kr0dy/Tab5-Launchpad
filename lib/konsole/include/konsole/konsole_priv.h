#ifndef KONSOLE_PRIV_H
#define KONSOLE_PRIV_H
#include "static.h"
void _kon_line_reset(struct konsole *ks);
void _kon_line_redraw(struct konsole *ks);
void _kon_line_insert(struct konsole *ks, char c);
void _kon_line_backspace(struct konsole *ks);
void _kon_line_delete(struct konsole *ks);
void _kon_line_move(struct konsole *ks, int delta);
void _kon_line_history(struct konsole *ks, int dir);
void _kon_line_add_history(struct konsole *ks, const char *s);
int  _kon_line_handle_esc(struct konsole *ks, uint8_t ch);
int  _kon_tokenize(char *line, char *argv[], int max);
const struct kon_cmd * _kon_find(const struct kon_cmd *t, size_t n, const char *name);
#endif
