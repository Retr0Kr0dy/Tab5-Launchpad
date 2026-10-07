#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "konsole/konsole.h"
#include "konsole/konsole_priv.h"
#include "konsole/version.h"

static int cmd_version(struct konsole *ks, int argc, char **argv) {
    (void)argc; (void)argv;
    kon_printf(ks, "konsole: %s (%d.%d.%d)\r\n",
        KONSOLE_VERSION_STRING, KONSOLE_VERSION_MAJOR, KONSOLE_VERSION_MINOR, KONSOLE_VERSION_PATCH);
    kon_printf(ks, "firmware: %s %s\r\n", KONSOLE_FW_NAME, KONSOLE_FW_VERSION);
    return 0;
}

static int cmd_clear(struct konsole *ks, int argc, char **argv) {
    (void)argc; (void)argv;
    kon_clear_screen(ks);
    return 0;
}

/* Weak hook: firmware can override this */
__attribute__((weak)) void konsole_on_reboot(void) {
    /* default: just print a message */
}

static int cmd_reboot(struct konsole *ks, int argc, char **argv) {
    (void)argc; (void)argv;
    kon_printf(ks, "rebooting...\r\n");
    konsole_on_reboot();
    return 0;
}

static int cmd_help_builtin(struct konsole *ks, int argc, char **argv) {
    (void)argc; (void)argv;
    kon_print_help(ks);
    return 0;
}

static const struct kon_cmd s_builtin_cmds[] = {
    { "help",   "list commands",      cmd_help_builtin },
    { "clear",  "clear the screen",   cmd_clear },
    { "version","show version info",  cmd_version },
    { "reboot", "reboot device",      cmd_reboot },
};
#define KONSOLE_BUILTIN_COUNT (sizeof(s_builtin_cmds)/sizeof(s_builtin_cmds[0]))


static inline void ts_puts(struct konsole *ks, const char *s) {
    if (ks->io.write) ks->io.write(ks->io.ctx, (const uint8_t*)s, strlen(s));
}
static inline void ts_write(struct konsole *ks, const uint8_t *b, size_t n) {
    if (ks->io.write) ks->io.write(ks->io.ctx, b, n);
}

/* Debug: global RX hex-dump flag */
static uint8_t g_kon_rxdbg = 0;
void kon_debug_rxdump(int on){ g_kon_rxdbg = on ? 1 : 0; }

void kon_printf(struct konsole *ks, const char *fmt, ...) {
#if KONSOLE_ENABLE_PRINTF
    va_list ap; va_start(ap, fmt);
    #if KONSOLE_USE_NPF
        extern int npf_vsnprintf(char *buffer, size_t bufsz, const char *format, va_list vlist);
        int n = npf_vsnprintf(ks->fmtbuf, sizeof(ks->fmtbuf), fmt, ap);
    #else
        int n = vsnprintf(ks->fmtbuf, sizeof(ks->fmtbuf), fmt, ap);
    #endif
    va_end(ap);
    if (n < 0) return;
    size_t len = (size_t)((n < (int)sizeof(ks->fmtbuf)) ? n : (int)sizeof(ks->fmtbuf));
    ts_write(ks, (const uint8_t*)ks->fmtbuf, len);
#else
    (void)ks; (void)fmt;
#endif
}

static void print_table(struct konsole *ks, const struct kon_cmd *t, size_t n) {
    for (size_t i = 0; i < n; i++) {
        kon_printf(ks, "  %-12s %s\r\n", t[i].name, t[i].help ? t[i].help : "");
    }
}

void kon_print_help(struct konsole *ks) {
    kon_printf(ks, "commands:\r\n");

    print_table(ks, ks->cmds, ks->cmd_count);

    for (size_t i = 0; i < KONSOLE_BUILTIN_COUNT; i++) {
        const struct kon_cmd *b = &s_builtin_cmds[i];
        if (_kon_find(ks->cmds, ks->cmd_count, b->name)) continue; /* firmware wins */
        kon_printf(ks, "  %-12s %s\r\n", b->name, b->help ? b->help : "");
    }
}

void kon_clear_screen(struct konsole *ks) {
#if KONSOLE_ENABLE_VT100
    if (ks->mode == KON_MODE_ANSI && ks->vt100) {
        ts_puts(ks, "\x1b[2J\x1b[H");
        ks->last_screen_len = 0;
        return;
    }
#endif
    for (int i=0;i<30;i++) ts_puts(ks, "\r\n");
    ks->last_screen_len = 0;
}

static void execute_line(struct konsole *ks) {
    struct kon_line_state *ls = ks->line;

    ls->line[ls->len] = '\0';

    char raw_line[KONSOLE_MAX_LINE];
    size_t raw_len = (ls->len < (KONSOLE_MAX_LINE - 1)) ? ls->len : (KONSOLE_MAX_LINE - 1);
    memcpy(raw_line, ls->line, raw_len);
    raw_line[raw_len] = '\0';

    ts_puts(ks, "\r\n");

#if KONSOLE_HISTORY > 0
    bool nonspace = false;
    for (size_t i = 0; i < raw_len; ++i) { if (raw_line[i] > ' ') { nonspace = true; break; } }
    if (nonspace) { _kon_line_add_history(ks, raw_line); }
#endif

    char line_copy[KONSOLE_MAX_LINE];

    memcpy(line_copy, raw_line, raw_len + 1);

    char *argv[KONSOLE_MAX_ARGS];
    int argc = _kon_tokenize(line_copy, argv, KONSOLE_MAX_ARGS);

    if (argc > 0) {
        const struct kon_cmd *cmd = _kon_find(ks->cmds, ks->cmd_count, argv[0]);
        if (!cmd) {
            cmd = _kon_find(s_builtin_cmds, KONSOLE_BUILTIN_COUNT, argv[0]);
        }
        if (cmd && cmd->fn) {
            cmd->fn(ks, argc, argv);
        } else {
            if (ks->on_unknown) ks->on_unknown(ks, raw_line);
            else kon_printf(ks, "unknown: %s\r\n", argv[0]);
        }
    }

    _kon_line_reset(ks);

#if KONSOLE_HISTORY > 0
    ks->line->hist_nav = -1;
#endif

    if (ks->prompt) _kon_line_redraw(ks);
}

void kon_feed(struct konsole *ks, const uint8_t *data, size_t len) {
    for (size_t i=0;i<len;i++) {
        if (g_kon_rxdbg) { char s[8]; snprintf(s,sizeof(s),"%02X ", data[i]); ts_puts(ks,s); }

        uint8_t c = data[i];

#if KONSOLE_EOL_MODE == 1
        /* Execute on CR or LF; if CRLF arrives, run once on CR and ignore the LF */
        if (c == '\r') { execute_line(ks); ks->saw_cr = 1; continue; }
        if (c == '\n') { if (ks->saw_cr) { ks->saw_cr = 0; continue; } execute_line(ks); continue; }
        ks->saw_cr = 0;
#else
        if (c == '\r') { ks->saw_cr = 1; continue; }
        if (c == '\n') { execute_line(ks); ks->saw_cr = 0; continue; }
        if (ks->saw_cr) { ks->saw_cr = 0; }
#endif

        if (_kon_line_handle_esc(ks, c)) continue;

        if (c == '\b' || c == 127) { _kon_line_backspace(ks); continue; }
        if (c == '\t') { _kon_line_insert(ks,' '); _kon_line_insert(ks,' '); continue; }
        if (c >= 32 && c < 127) { _kon_line_insert(ks,(char)c); continue; }
    }
}

void konsole_poll(struct konsole *ks) {
    uint8_t buf[64];
    size_t avail = ks->io.read_avail ? ks->io.read_avail(ks->io.ctx) : 1;
    if (!avail) return;
    size_t n = ks->io.read ? ks->io.read(ks->io.ctx, buf, sizeof(buf)) : 0;
    if (n) kon_feed(ks, buf, n);
}

void kon_banner(struct konsole *ks, const char *banner) {
    if (banner) { ts_puts(ks, banner); ts_puts(ks, "\r\n"); }
    if (ks->prompt) _kon_line_redraw(ks);
}

void konsole_init(struct konsole *ks, const struct konsole_io *io,
                  const struct kon_cmd *cmds, size_t cmd_count,
                  const char *prompt, bool vt100) {
    memset(ks, 0, sizeof(*ks));
    if (io) ks->io = *io;
    ks->cmds = cmds; ks->cmd_count = cmd_count;
    ks->prompt = prompt;
    ks->vt100 = vt100;
    ks->mode  = KON_MODE_ANSI; /* default safe mode */
    ks->line = (struct kon_line_state*)calloc(1, sizeof(*ks->line));
    _kon_line_reset(ks);
}

void konsole_init_with_storage(struct konsole *ks, struct kon_line_state *storage,
                               const struct konsole_io *io,
                               const struct kon_cmd *cmds, size_t cmd_count,
                               const char *prompt, bool vt100) {
    memset(ks, 0, sizeof(*ks));
    if (io) ks->io = *io;
    ks->cmds = cmds; ks->cmd_count = cmd_count;
    ks->prompt = prompt;
    ks->vt100 = vt100;
    ks->mode  = KON_MODE_ANSI; /* default safe mode */
    ks->line = storage;
    if (storage) memset(storage, 0, sizeof(*storage));
    _kon_line_reset(ks);
}

void konsole_set_mode(struct konsole *ks, kon_mode_t m){ ks->mode = m; }
kon_mode_t konsole_get_mode(struct konsole *ks){ return ks->mode; }
