/* X360 SoR1: no console não há seletor de arquivos. A ROM do usuário fica ao lado do default.xex
 * (game:\) ou em game:\rom\, com qualquer nome .bin/.md/.gen/.smd; a primeira que o runtime aceita
 * (tamanho e SHA-1, bus_load_rom) é usada e lembrada no settings.ini. */
#include <stdio.h>
#include <string.h>
#include "dirent.h"
#include "strings.h"

const char *bus_load_rom(const char *path);

static int rom_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".bin") || !strcasecmp(dot, ".md") || !strcasecmp(dot, ".gen") ||
                   !strcasecmp(dot, ".smd"));
}

int x360_find_rom(char *out, size_t n) {
    static const char *const dirs[] = { "game:\\", "game:\rom\\" };
    for (int k = 0; k < 2; k++) {
        DIR *d = opendir(dirs[k]);
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d))) {
            char path[400];
            if (!rom_ext(e->d_name)) continue;
            snprintf(path, sizeof path, "%s%s", dirs[k], e->d_name);
            if (!bus_load_rom(path)) { snprintf(out, n, "%s", path); closedir(d); return 1; }
        }
        closedir(d);
    }
    return 0;
}
