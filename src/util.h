#ifndef ABC_UTIL_H
#define ABC_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

unsigned char *read_file(const char *path, size_t *len_out);
int write_file(const char *path, const void *data, size_t len);

bool str_ieq(const char *a, const char *b);
bool str_iends(const char *s, const char *suffix);
char *str_lower(char *s);

/* Resolve a Windows-style scene path ("\\WAV\\7ck.WAV") inside root,
   case-insensitively. Returns a malloc'ed POSIX path. */
char *resolve_asset(const char *root, const char *win_path);

/* Join root + "/" + rel. */
char *path_join(const char *root, const char *rel);

/* Create directory (ignoring EEXIST). */
int mkdir_p(const char *path);

#endif
