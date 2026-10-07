#include <string.h>
#include "konsole/konsole.h"
#include "konsole/konsole_priv.h"

int _kon_tokenize(char *line, char *argv[], int max) {
    int argc = 0; char *p = line; int inq = 0; char *start = NULL;
    while (*p) {
        if (*p == '"') {
            inq = !inq;
            if (inq) { start = p + 1; }
            else { *p = '\0'; argv[argc++] = start; if (argc == max) return argc; start = NULL; }
        } else if (!inq && (*p == ' ' || *p == '\t')) {
            if (start) { *p = '\0'; argv[argc++] = start; if (argc == max) return argc; start = NULL; }
        } else if (!start) { start = p; }
        p++;
    }
    if (start && argc < max) argv[argc++] = start;
    return argc;
}

const struct kon_cmd * _kon_find(const struct kon_cmd *t, size_t n, const char *name) {
    for (size_t i=0;i<n;i++) if (strcmp(t[i].name, name) == 0) return &t[i];
    return NULL;
}
