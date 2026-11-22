/* X360 SoR1: o pouco de POSIX que o runtime usa no caminho não Windows. */
#include <xtl.h>
#include <stdlib.h>
#include <string.h>
#include "dirent.h"
#include "strings.h"

struct X360_DIR { HANDLE h; WIN32_FIND_DATA fd; int first; struct dirent ent; };

DIR *opendir(const char *path) {
    char pattern[300];
    size_t n = strlen(path);
    DIR *d;
    if (n + 3 > sizeof pattern) return NULL;
    memcpy(pattern, path, n);
    for (size_t i = 0; i < n; i++) if (pattern[i] == '/') pattern[i] = 0x5C; /* barra invertida */
    if (n && pattern[n - 1] != 0x5C) pattern[n++] = 0x5C;
    pattern[n++] = '*'; pattern[n] = 0;
    d = (DIR *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->h = FindFirstFile(pattern, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}
struct dirent *readdir(DIR *d) {
    if (!d) return NULL;
    if (!d->first && !FindNextFile(d->h, &d->fd)) return NULL;
    d->first = 0;
    strncpy(d->ent.d_name, d->fd.cFileName, sizeof d->ent.d_name - 1);
    d->ent.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &d->ent;
}
int closedir(DIR *d) { if (d) { FindClose(d->h); free(d); } return 0; }
/* A folder (save states): the console's file system takes only backslashes. */
int x360_mkdir(const char *path) {
    char p[300];
    size_t n = strlen(path);
    if (n >= sizeof p) return -1;
    for (size_t i = 0; i <= n; i++) p[i] = path[i] == '/' ? 0x5C : path[i];
    return CreateDirectoryA(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}
int strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
int strncasecmp(const char *a, const char *b, size_t n) { return _strnicmp(a, b, n); }
