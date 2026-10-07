#include "settings_store.h"
#include "orion.h"
#include "ui_theme.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "sic/sic.h"
#include "sic/storage/sd.h"

#define ORION_SETTINGS_SCHEMA 1

static int sd_ready(void)
{
    const sd_t* sd = sic_sd(0);
    if (!sd || !sd->v) return 0;
    if (sd->v->begin) (void)sd->v->begin(sd);
    return sd->v->present && sd->v->present(sd) == 1;
}

static char* trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static int parse_int_range(const char* s, int lo, int hi, int* out)
{
    if (!s || !out) return -1;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *trim(end) != '\0' || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

int orion_settings_load_sd(void)
{
    if (!sd_ready()) return -1;

    FILE* fp = fopen(ORION_SETTINGS_DEFAULT_SD_PATH, "r");
    if (!fp) return -2;

    char line[96];
    int saw_schema = 0;
    while (fgets(line, sizeof line, fp)) {
        char* p = trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[') continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(p);
        char* val = trim(eq + 1);
        int v;

        if (strcmp(key, "schema") == 0) {
            if (parse_int_range(val, 1, ORION_SETTINGS_SCHEMA, &v) == 0) saw_schema = 1;
            continue;
        }
        /* Each key is applied independently as soon as it parses -- unlike
         * the theme file (one atomic struct), these are unrelated
         * preferences, so a bad/out-of-range value for one shouldn't cost
         * the others. Ranges match each setting's own real setter clamp
         * (orion_ui_set_scale/_set_grid_cols already clamp internally, but
         * validating here means a garbled line is silently skipped instead
         * of clamped to a value the file never actually asked for). */
        if (strcmp(key, "ui_scale") == 0) {
            if (parse_int_range(val, 0, 2, &v) == 0) orion_ui_set_scale(v);
        } else if (strcmp(key, "grid_cols") == 0) {
            if (parse_int_range(val, 1, 6, &v) == 0) orion_ui_set_grid_cols(v);
        } else if (strcmp(key, "backlight_pct") == 0) {
            if (parse_int_range(val, 0, 100, &v) == 0) orion_backlight_set(v);
        } else if (strcmp(key, "auto_rotation") == 0) {
            if (parse_int_range(val, 0, 1, &v) == 0) orion_display_set_auto_rotation(v);
        } else if (strcmp(key, "mem_badge") == 0) {
            if (parse_int_range(val, 0, 1, &v) == 0) orion_ui_set_mem_badge(v);
        } else if (strcmp(key, "sleep_timeout_idx") == 0) {
            if (parse_int_range(val, 0, 2, &v) == 0) orion_ui_set_sleep_timeout_idx(v);
        }
        /* Unknown keys ignored for forward compatibility, same as theme.ini. */
    }
    fclose(fp);
    return saw_schema ? 0 : -3;
}

int orion_settings_save_sd(void)
{
    if (!sd_ready()) return -1;

    /* Best-effort: the directory may not exist yet if the user has never
     * dropped a theme.ini here either. EEXIST is the expected/common case
     * and not an error; anything else just means the fopen() below will
     * fail too, which is handled the normal way. */
    if (mkdir("/sdcard/orion", 0777) != 0 && errno != EEXIST) {
        /* fall through -- fopen will report the real failure */
    }

    FILE* fp = fopen(ORION_SETTINGS_DEFAULT_SD_PATH, "w");
    if (!fp) return -2;

    fprintf(fp, "schema=%d\n", ORION_SETTINGS_SCHEMA);
    fprintf(fp, "ui_scale=%d\n", orion_ui_get_scale());
    fprintf(fp, "grid_cols=%d\n", orion_ui_get_grid_cols());
    fprintf(fp, "backlight_pct=%d\n", orion_backlight_get());
    fprintf(fp, "auto_rotation=%d\n", orion_display_auto_rotation_enabled());
    fprintf(fp, "mem_badge=%d\n", orion_ui_get_mem_badge());
    fprintf(fp, "sleep_timeout_idx=%d\n", orion_ui_get_sleep_timeout_idx());

    fclose(fp);
    return 0;
}
