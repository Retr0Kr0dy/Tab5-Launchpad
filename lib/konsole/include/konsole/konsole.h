#ifndef KONSOLE_KONSOLE_H
#define KONSOLE_KONSOLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KONSOLE_MAX_LINE
#define KONSOLE_MAX_LINE 128
#endif
#ifndef KONSOLE_MAX_ARGS
#define KONSOLE_MAX_ARGS 8
#endif
#ifndef KONSOLE_HISTORY
#define KONSOLE_HISTORY 8
#endif
#ifndef KONSOLE_ENABLE_VT100
#define KONSOLE_ENABLE_VT100 1
#endif
#ifndef KONSOLE_ENABLE_PRINTF
#define KONSOLE_ENABLE_PRINTF 1
#endif

/* EOL mode: 1 = CR or LF (ignore 2nd in CRLF), 0 = LF only */
#ifndef KONSOLE_EOL_MODE
#define KONSOLE_EOL_MODE 1
#endif

struct konsole;
typedef int (*kon_cmd_fn)(struct konsole *ks, int argc, char **argv);
typedef int (*kon_unknown_fn)(struct konsole *ks, const char *line);

typedef enum { KON_MODE_COMPAT = 0, KON_MODE_ANSI = 1 } kon_mode_t;

struct kon_cmd { const char *name; const char *help; kon_cmd_fn fn; };

struct konsole {
    struct konsole_io io;
    const struct kon_cmd *cmds;
    size_t cmd_count;
    const char *prompt;
    bool vt100;
    kon_mode_t mode;          /* runtime mode: compat(default) or ansi */
    char fmtbuf[256];
    struct kon_line_state *line;
    /* runtime */
    uint8_t saw_cr;
    size_t  last_screen_len;
    kon_unknown_fn on_unknown;
};

/* API */
void konsole_init(struct konsole *ks, const struct konsole_io *io,
                  const struct kon_cmd *cmds, size_t cmd_count,
                  const char *prompt, bool vt100);

struct kon_line_state;
void konsole_init_with_storage(struct konsole *ks, struct kon_line_state *storage,
                               const struct konsole_io *io,
                               const struct kon_cmd *cmds, size_t cmd_count,
                               const char *prompt, bool vt100);

void konsole_poll(struct konsole *ks);
void kon_printf(struct konsole *ks, const char *fmt, ...);
void kon_banner(struct konsole *ks, const char *banner);
void kon_feed(struct konsole *ks, const uint8_t *data, size_t len);

void kon_print_help(struct konsole *ks);
void kon_clear_screen(struct konsole *ks);

/* Mode control */
void konsole_set_mode(struct konsole *ks, kon_mode_t m);
kon_mode_t konsole_get_mode(struct konsole *ks);

/* Optional: set a fallback for unknown commands */
static inline void konsole_set_unknown_handler(struct konsole *ks, kon_unknown_fn fn) { ks->on_unknown = fn; }

/* Debug helper: toggle RX hex dump */
void kon_debug_rxdump(int on);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KONSOLE_KONSOLE_H */
