#define _GNU_SOURCE
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __android_log_write(ANDROID_LOG_INFO, "abc", buf);
#else
    fputs("[abc] ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
#endif
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __android_log_write(ANDROID_LOG_WARN, "abc", buf);
#else
    fputs("[abc] warning: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
#endif
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __android_log_write(ANDROID_LOG_ERROR, "abc", buf);
#else
    fputs("[abc] error: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
#endif
    va_end(ap);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        log_error("out of memory");
        abort();
    }
    return p;
}

void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) {
        log_error("out of memory");
        abort();
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        log_error("out of memory");
        abort();
    }
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

unsigned char *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    unsigned char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = 0;
    if (len_out)
        *len_out = (size_t)n;
    return buf;
}

int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t got = fwrite(data, 1, len, f);
    int rc = fclose(f);
    return (got == len && rc == 0) ? 0 : -1;
}

bool str_ieq(const char *a, const char *b)
{
    return a && b && strcasecmp(a, b) == 0;
}

bool str_iends(const char *s, const char *suffix)
{
    if (!s || !suffix)
        return false;
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcasecmp(s + ls - lf, suffix) == 0;
}

char *str_lower(char *s)
{
    for (char *p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    return s;
}

char *path_join(const char *root, const char *rel)
{
    size_t a = strlen(root), b = strlen(rel);
    char *out = xmalloc(a + b + 2);
    memcpy(out, root, a);
    size_t n = a;
    if (n && out[n - 1] != '/')
        out[n++] = '/';
    memcpy(out + n, rel, b + 1);
    return out;
}

static char *find_ci(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    struct dirent *e;
    char *found = NULL;
    while ((e = readdir(d))) {
        if (strcasecmp(e->d_name, name) == 0) {
            found = xstrdup(e->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

char *resolve_asset(const char *root, const char *win_path)
{
    if (!win_path || !*win_path) {
        return xstrdup("");
    }
    /* Normalise separators and strip a leading separator/Drive. */
    char *clean = xstrdup(win_path);
    for (char *p = clean; *p; p++)
        if (*p == '\\')
            *p = '/';
    char *start = clean;
    while (*start == '/')
        start++;
    if (start[0] && start[1] == ':')
        start += 2;
    while (*start == '/')
        start++;

    char *result = xstrdup(root);
    char *save = NULL;
    char *seg = strtok_r(start, "/", &save);
    while (seg) {
        char *real = find_ci(result, seg);
        char *next;
        if (real) {
            next = path_join(result, real);
            free(real);
        } else {
            next = path_join(result, seg);
        }
        free(result);
        result = next;
        seg = strtok_r(NULL, "/", &save);
    }
    free(clean);
    return result;
}

bool path_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0;
}

char *resolve_asset_fallback(const char *root, const char *addon, const char *win_path)
{
    char *primary = resolve_asset(root, win_path);
    if (path_exists(primary))
        return primary;
    if (addon && *addon) {
        char *fallback = resolve_asset(addon, win_path);
        if (path_exists(fallback)) {
            free(primary);
            return fallback;
        }
        free(fallback);
    }
    return primary;
}

int mkdir_p(const char *path)
{
    char *tmp = xstrdup(path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = 0;
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        rc = -1;
    free(tmp);
    return rc;
}
