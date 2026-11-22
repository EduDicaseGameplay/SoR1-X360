/* X360 SoR2: opendir/readdir/closedir sobre FindFirstFile do XDK (music.c, texpack.c). */
#ifndef X360_DIRENT_H
#define X360_DIRENT_H
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent { char d_name[260]; unsigned char d_type; };
typedef struct X360_DIR DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
