#ifndef ABC_DFM_H
#define ABC_DFM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DFM_NULL = 0,
    DFM_INT,
    DFM_FLOAT,
    DFM_STR,
    DFM_LIST,   /* list or set; items are values (sets contain strings) */
    DFM_BIN,
    DFM_BOOL
} DfmType;

typedef struct DfmValue {
    DfmType type;
    long long i;
    double f;
    char *s;            /* NUL-terminated (CP1250/UTF-8 bytes) */
    unsigned char *bin;
    int bin_len;
    struct DfmValue *items;
    int count;
} DfmValue;

typedef struct DfmProp {
    char *name;
    DfmValue v;
} DfmProp;

typedef struct DfmComponent {
    char *cls;
    char *name;
    DfmProp *props;
    int nprops;
    struct DfmComponent **children;
    int nchildren;
} DfmComponent;

/* Parse a concatenated-record container (FRM file or RCDATA form). */
DfmComponent **dfm_parse(const unsigned char *data, size_t len, int *count_out);
void dfm_free(DfmComponent **recs, int count);

DfmValue *dfm_prop(const DfmComponent *c, const char *name);
long long dfm_int(const DfmComponent *c, const char *name, long long def);
bool dfm_bool(const DfmComponent *c, const char *name, bool def);
const char *dfm_str(const DfmComponent *c, const char *name, const char *def);
bool dfm_has(const DfmValue *v, const char *s);

#endif
