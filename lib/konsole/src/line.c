#include <string.h>
#include <stdio.h>
#include "konsole/konsole.h"
#include "konsole/konsole_priv.h"

static inline void ts_puts(struct konsole *ks, const char *s) {
    if (ks->io.write) ks->io.write(ks->io.ctx, (const uint8_t*)s, strlen(s));
}

static void redraw(struct konsole *ks) {
        struct kon_line_state *ls = ks->line;
    const size_t prompt_len = ks->prompt ? strlen(ks->prompt) : 0;
#if KONSOLE_ENABLE_VT100
    if (ks->mode == KON_MODE_ANSI && ks->vt100) {
            /* Hard redraw: CR, prompt, buffer, clear-to-EOL, then fix cursor */
        ts_puts(ks, "\r");
        if (ks->prompt) ts_puts(ks, ks->prompt);
        if (ls->len) ks->io.write(ks->io.ctx, (const uint8_t*)ls->line, ls->len);
        ts_puts(ks, "\x1b[K"); /* CSI K (0K): clear from cursor to end of line */
        size_t want = prompt_len + ls->cursor;
        size_t have = prompt_len + ls->len;
        if (have > want) {
                char seq[16]; snprintf(seq, sizeof seq, "\x1b[%zuD", have - want);
            ts_puts(ks, seq);
        } else if (want > have) {
                char seq[16]; snprintf(seq, sizeof seq, "\x1b[%zuC", want - have);
            ts_puts(ks, seq);
        }
        ks->last_screen_len = prompt_len + ls->len;
        return;
    }
#endif
    /* Non-ANSI fallback: wipe old content area and reprint */
    ts_puts(ks, "\r");
    for (size_t i = 0; i < ks->last_screen_len + 32; ++i) ts_puts(ks, " ");
    ts_puts(ks, "\r");
    if (ks->prompt) ts_puts(ks, ks->prompt);
    if (ls->len) ks->io.write(ks->io.ctx, (const uint8_t*)ls->line, ls->len);
    ks->last_screen_len = prompt_len + ls->len;
}

void _kon_line_redraw(struct konsole *ks) { redraw(ks); }


void _kon_line_reset(struct konsole *ks) {
    struct kon_line_state *ls = ks->line;
    ls->len = 0;
    ls->cursor = 0;
    if (ls->line[0]) ls->line[0] = '\0';
    ks->last_screen_len = 0;
}

void _kon_line_insert(struct konsole *ks, char c) {
    struct kon_line_state *ls = ks->line;
    if (ls->len + 1 >= KONSOLE_MAX_LINE) return;
    /* Non-ANSI: append-only editing to keep logic simple */
    if (!(ks->mode == KON_MODE_ANSI && ks->vt100)) ls->cursor = ls->len;
    for (size_t i = ls->len; i > ls->cursor; i--) ls->line[i] = ls->line[i-1];
    ls->line[ls->cursor] = c;
    ls->len++; ls->cursor++;
    redraw(ks);
}

void _kon_line_backspace(struct konsole *ks) {
    struct kon_line_state *ls = ks->line;
    if (!ls->cursor) return;
    /* In compat, force end to keep behavior predictable */
    if (!(ks->mode == KON_MODE_ANSI && ks->vt100)) {
        if (ls->cursor != ls->len) ls->cursor = ls->len;
    }
    for (size_t i = ls->cursor - 1; i < ls->len - 1; i++) ls->line[i] = ls->line[i+1];
    ls->len--; ls->cursor--;
    ls->line[ls->len] = '\0';
    redraw(ks);
}

void _kon_line_delete(struct konsole *ks) {
    struct kon_line_state *ls = ks->line;
    if (ls->cursor >= ls->len) return;
    /* Only meaningful in ANSI; ignore in compat */
    if (!(ks->mode == KON_MODE_ANSI && ks->vt100)) return;
    for (size_t i = ls->cursor; i < ls->len - 1; i++) ls->line[i] = ls->line[i+1];
    ls->len--;
    ls->line[ls->len] = '\0';
    redraw(ks);
}

void _kon_line_move(struct konsole *ks, int delta) {
    /* Cursor move only in ANSI mode */
    if (!(ks->mode == KON_MODE_ANSI && ks->vt100)) return;
    struct kon_line_state *ls = ks->line;
    int np = (int)ls->cursor + delta;
    if (np < 0) np = 0;
    if (np > (int)ls->len) np = (int)ls->len;
    if ((size_t)np == ls->cursor) return;
#if KONSOLE_ENABLE_VT100
    if (ks->vt100) {
        char seq[16];
        if (np < (int)ls->cursor) snprintf(seq, sizeof(seq), "\x1b[%uD", (unsigned)(ls->cursor - np));
        else snprintf(seq, sizeof(seq), "\x1b[%uC", (unsigned)(np - ls->cursor));
        ts_puts(ks, seq);
    }
#endif
    ls->cursor = (size_t)np;
}

#if KONSOLE_HISTORY > 0
static void add_history(struct konsole *ks, const char *s) {
    struct kon_line_state *ls = ks->line;
    if (!ls->len) return;
    int last = (ls->hist_head - 1 + KONSOLE_HISTORY) % KONSOLE_HISTORY;
    if (ls->hist_count > 0 && strncmp(ls->hist[last], s, KONSOLE_MAX_LINE) == 0) return;
    strncpy(ls->hist[ls->hist_head], s, KONSOLE_MAX_LINE - 1);
    ls->hist[ls->hist_head][KONSOLE_MAX_LINE - 1] = '\0';
    ls->hist_head = (ls->hist_head + 1) % KONSOLE_HISTORY;
    if (ls->hist_count < KONSOLE_HISTORY) ls->hist_count++;
}

void _kon_line_add_history(struct konsole *ks, const char *s) {
        struct kon_line_state *ls = ks->line;
#if KONSOLE_HISTORY <= 0
    (void)ks; (void)s; return;
#else
    if (!s || !*s) return;
    /* Dedup against last entry */
    if (ls->hist_count > 0) {
            int last = ls->hist_head - 1; if (last < 0) last = KONSOLE_HISTORY - 1;
        if (strncmp(ls->hist[last], s, KONSOLE_MAX_LINE) == 0) {
                ls->hist_nav = -1; return;
        }
    }
    strncpy(ls->hist[ls->hist_head], s, KONSOLE_MAX_LINE);
    ls->hist[ls->hist_head][KONSOLE_MAX_LINE-1] = '\0';
    ls->hist_head = (ls->hist_head + 1) % KONSOLE_HISTORY;
    if (ls->hist_count < KONSOLE_HISTORY) ls->hist_count++;
    ls->hist_nav = -1;
#endif
}

/* HISTORY NAVIGATION */
void _kon_line_history(struct konsole *ks, int dir) {
        struct kon_line_state *ls = ks->line;
#if KONSOLE_HISTORY <= 0
    (void)dir; (void)ls;
    return;
#else
    if (ls->hist_count == 0) return;
    /* dir < 0 => Up (older). dir > 0 => Down (newer). */
    if (dir < 0) {
            if (ls->hist_nav == -1) {
                /* First time entering history: stash current edit buffer in head slot */
            strncpy(ls->hist[ls->hist_head], ls->line, KONSOLE_MAX_LINE);
            ls->hist[ls->hist_head][KONSOLE_MAX_LINE-1] = '\0';
            ls->hist_nav = 0;
        } else if (ls->hist_nav < ls->hist_count - 1) {
                ls->hist_nav++;
        }
    } else if (dir > 0) {
            if (ls->hist_nav == -1) return; /* already live */
        if (ls->hist_nav > 0) {
                ls->hist_nav--;
        } else {
                /* Leaving history: restore stash (live buffer) */
            ls->hist_nav = -1;
            strncpy(ls->line, ls->hist[ls->hist_head], KONSOLE_MAX_LINE);
            ls->line[KONSOLE_MAX_LINE-1] = '\0';
            ls->len = strlen(ls->line);
            ls->cursor = ls->len;
            redraw(ks);
            return;
        }
    } else {
            return;
    }
    /* Map nav offset to circular index: 0 => most recent entry */
    int idx = ls->hist_head - 1 - ls->hist_nav;
    if (idx < 0) idx += KONSOLE_HISTORY;
    strncpy(ls->line, ls->hist[idx], KONSOLE_MAX_LINE);
    ls->line[KONSOLE_MAX_LINE-1] = '\0';
    ls->len = strlen(ls->line);
    ls->cursor = ls->len;
    redraw(ks);
#endif
}
#else
void _kon_line_add_history(struct konsole *ks, const char *s) { (void)ks; (void)s; }
void _kon_line_history(struct konsole *ks, int dir) { (void)ks; (void)dir; }
#endif

int _kon_line_handle_esc(struct konsole *ks, uint8_t ch) {
    static uint8_t s = 0;
    if (ch == '\x1b') { s = 1; return 1; }
    if (s == 1) { if (ch == '[') { s = 2; return 1; } s = 0; return 0; } /* lone ESC no longer eats next char */
    if (s == 2) {
#if KONSOLE_HISTORY > 0
        if (ch >= 'A' && ch <= 'D') {
            switch (ch) {
                case 'A': _kon_line_history(ks, -1); break;
                case 'B': _kon_line_history(ks, +1); break;
                case 'C': _kon_line_move(ks, +1);     break;
                case 'D': _kon_line_move(ks, -1);     break;
            }
            s = 0; return 1;
        }
#else
        if (ch == 'C' || ch == 'D') { _kon_line_move(ks, ch=='C'?+1:-1); s = 0; return 1; }
#endif
        if (ch == '3') { s = 3; return 1; }
        s = 0; return 1;
    }
    if (s == 3) { if (ch == '~') _kon_line_delete(ks); s = 0; return 1; }
    return 0;
}
