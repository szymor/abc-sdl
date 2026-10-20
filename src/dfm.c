#include "dfm.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const unsigned char *b;
    size_t len;
    size_t i;
} Reader;

static unsigned char ru8(Reader *r)
{
    if (r->i >= r->len)
        return 0;
    return r->b[r->i++];
}

static unsigned char *rraw(Reader *r, size_t n)
{
    if (r->i + n > r->len)
        n = r->len - r->i;
    unsigned char *p = xmalloc(n + 1);
    memcpy(p, r->b + r->i, n);
    p[n] = 0;
    r->i += n;
    return p;
}

static int16_t ri16(Reader *r)
{
    unsigned char *p = rraw(r, 2);
    int16_t v = (int16_t)(p[0] | (p[1] << 8));
    free(p);
    return v;
}

static int32_t ri32(Reader *r)
{
    unsigned char *p = rraw(r, 4);
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    free(p);
    return v;
}

static float rf32(Reader *r)
{
    int32_t v = ri32(r);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static double rf64(Reader *r)
{
    unsigned char *p = rraw(r, 8);
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--)
        u = (u << 8) | p[i];
    double d;
    memcpy(&d, &u, 8);
    free(p);
    return d;
}

static double rext80(Reader *r)
{
    unsigned char *p = rraw(r, 10);
    uint64_t m = 0;
    for (int i = 7; i >= 0; i--)
        m = (m << 8) | p[i];
    unsigned se = (unsigned)p[8] | ((unsigned)p[9] << 8);
    free(p);
    int sign = (se & 0x8000) ? -1 : 1;
    int exp = se & 0x7fff;
    if (exp == 0 && m == 0)
        return 0.0;
    if (exp == 0x7fff)
        return sign > 0 ? 1e300 : -1e300;
    return sign * (double)m * pow(2.0, (double)(exp - 16383 - 63));
}

static char *rstring(Reader *r)
{
    int n = ru8(r);
    unsigned char *p = rraw(r, (size_t)n);
    p[n] = 0;
    return (char *)p;
}

static void utf16_to_utf8(const unsigned char *in, int nchars, char **out)
{
    /* Minimal UTF-16LE -> UTF-8 (BMP only, surrogates become '?'). */
    char *buf = xmalloc((size_t)nchars * 4 + 1);
    int o = 0;
    for (int i = 0; i < nchars; i++) {
        unsigned c = (unsigned)in[i * 2] | ((unsigned)in[i * 2 + 1] << 8);
        if (c < 0x80)
            buf[o++] = (char)c;
        else if (c < 0x800) {
            buf[o++] = (char)(0xc0 | (c >> 6));
            buf[o++] = (char)(0x80 | (c & 0x3f));
        } else if (c >= 0xd800 && c <= 0xdfff) {
            buf[o++] = '?';
        } else {
            buf[o++] = (char)(0xe0 | (c >> 12));
            buf[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            buf[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    buf[o] = 0;
    *out = buf;
}

static void value_free(DfmValue *v);

static void value_init(DfmValue *v, DfmType t)
{
    memset(v, 0, sizeof(*v));
    v->type = t;
}

static bool read_value(Reader *r, DfmValue *out);

static void read_props(Reader *r, DfmProp **props_out, int *count_out)
{
    DfmProp *props = NULL;
    int n = 0, cap = 0;
    for (;;) {
        char *name = rstring(r);
        if (!name[0]) {
            free(name);
            break;
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            props = xrealloc(props, (size_t)cap * sizeof(*props));
        }
        props[n].name = name;
        read_value(r, &props[n].v);
        n++;
    }
    *props_out = props;
    *count_out = n;
}

static bool read_value(Reader *r, DfmValue *out)
{
    unsigned char t = ru8(r);
    switch (t) {
    case 0:
        value_init(out, DFM_NULL);
        return true;
    case 1: {
        value_init(out, DFM_LIST);
        int cap = 0, n = 0;
        DfmValue *items = NULL;
        for (;;) {
            DfmValue v;
            read_value(r, &v);
            if (v.type == DFM_NULL) {
                value_free(&v);
                break;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                items = xrealloc(items, (size_t)cap * sizeof(*items));
            }
            items[n++] = v;
        }
        out->items = items;
        out->count = n;
        return true;
    }
    case 2: {
        unsigned char *p = rraw(r, 1);
        value_init(out, DFM_INT);
        out->i = (int8_t)p[0];
        free(p);
        return true;
    }
    case 3:
        value_init(out, DFM_INT);
        out->i = ri16(r);
        return true;
    case 4:
        value_init(out, DFM_INT);
        out->i = ri32(r);
        return true;
    case 5:
        value_init(out, DFM_FLOAT);
        out->f = rext80(r);
        return true;
    case 6:
    case 7:
        value_init(out, DFM_STR);
        out->s = rstring(r);
        return true;
    case 8:
        value_init(out, DFM_BOOL);
        out->i = 0;
        return true;
    case 9:
        value_init(out, DFM_BOOL);
        out->i = 1;
        return true;
    case 10: {
        int32_t n = ri32(r);
        if (n < 0)
            n = 0;
        value_init(out, DFM_BIN);
        out->bin = rraw(r, (size_t)n);
        out->bin_len = n;
        return true;
    }
    case 11: {
        value_init(out, DFM_LIST);
        int cap = 0, n = 0;
        DfmValue *items = NULL;
        for (;;) {
            char *s = rstring(r);
            if (!s[0]) {
                free(s);
                break;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 4;
                items = xrealloc(items, (size_t)cap * sizeof(*items));
            }
            value_init(&items[n], DFM_STR);
            items[n].s = s;
            n++;
        }
        out->items = items;
        out->count = n;
        return true;
    }
    case 12: {
        /* collection: item type byte then a property block, until type 0 */
        value_init(out, DFM_LIST);
        int cap = 0, n = 0;
        DfmValue *items = NULL;
        for (;;) {
            unsigned char itemtype = ru8(r);
            if (itemtype == 0)
                break;
            DfmProp *props = NULL;
            int nprops = 0;
            read_props(r, &props, &nprops);
            if (n == cap) {
                cap = cap ? cap * 2 : 4;
                items = xrealloc(items, (size_t)cap * sizeof(*items));
            }
            value_init(&items[n], DFM_LIST);
            items[n].count = nprops;
            items[n].items = xcalloc((size_t)nprops + 1, sizeof(DfmValue));
            for (int i = 0; i < nprops; i++) {
                value_init(&items[n].items[i], DFM_STR);
                items[n].items[i].s = xstrdup(props[i].name);
                value_free(&props[i].v);
                free(props[i].name);
            }
            free(props);
            n++;
        }
        out->items = items;
        out->count = n;
        return true;
    }
    case 13:
        value_init(out, DFM_FLOAT);
        out->f = rf32(r);
        return true;
    case 14: {
        int32_t hi = ri32(r), lo = ri32(r);
        int64_t v = ((int64_t)hi << 32) | (uint32_t)lo;
        value_init(out, DFM_FLOAT);
        out->f = (double)v / 10000.0;
        return true;
    }
    case 15:
        value_init(out, DFM_FLOAT);
        out->f = rf64(r);
        return true;
    case 16: {
        int32_t n = ri32(r);
        if (n < 0)
            n = 0;
        unsigned char *raw = rraw(r, (size_t)n * 2);
        value_init(out, DFM_STR);
        utf16_to_utf8(raw, n, &out->s);
        free(raw);
        return true;
    }
    case 17: {
        int32_t hi = ri32(r), lo = ri32(r);
        value_init(out, DFM_INT);
        out->i = (int64_t)(((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo);
        return true;
    }
    case 18: {
        int32_t n = ri32(r);
        if (n < 0)
            n = 0;
        unsigned char *raw = rraw(r, (size_t)n);
        value_init(out, DFM_STR);
        out->s = (char *)raw;
        return true;
    }
    case 19:
        value_init(out, DFM_FLOAT);
        out->f = rf64(r);
        return true;
    default:
        value_init(out, DFM_NULL);
        return false;
    }
}

static void value_free(DfmValue *v)
{
    if (!v)
        return;
    free(v->s);
    free(v->bin);
    for (int i = 0; i < v->count; i++)
        value_free(&v->items[i]);
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static DfmComponent *read_component(Reader *r, bool top_level)
{
    DfmComponent *c = xcalloc(1, sizeof(*c));
    if (top_level) {
        /* signature already consumed */
    }
    c->cls = rstring(r);
    c->name = rstring(r);
    read_props(r, &c->props, &c->nprops);

    int cap = 0;
    for (;;) {
        unsigned char cls_first = r->i < r->len ? r->b[r->i] : 0;
        if (cls_first == 0) {
            ru8(r);
            break;
        }
        if (c->nchildren == cap) {
            cap = cap ? cap * 2 : 4;
            c->children = xrealloc(c->children, (size_t)cap * sizeof(*c->children));
        }
        c->children[c->nchildren++] = read_component(r, false);
    }
    return c;
}

static void component_free(DfmComponent *c)
{
    if (!c)
        return;
    free(c->cls);
    free(c->name);
    for (int i = 0; i < c->nprops; i++) {
        free(c->props[i].name);
        value_free(&c->props[i].v);
    }
    free(c->props);
    for (int i = 0; i < c->nchildren; i++)
        component_free(c->children[i]);
    free(c->children);
    free(c);
}

DfmComponent **dfm_parse(const unsigned char *data, size_t len, int *count_out)
{
    DfmComponent **recs = NULL;
    int n = 0, cap = 0;
    size_t i = 0;
    while (i < len) {
        if (len - i >= 4 && memcmp(data + i, "TPF0", 4) == 0) {
            Reader r = {data, len, i + 4};
            DfmComponent *c = read_component(&r, true);
            i = r.i;
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                recs = xrealloc(recs, (size_t)cap * sizeof(*recs));
            }
            recs[n++] = c;
        } else if (data[i] == 0) {
            i++;
        } else {
            /* Unknown padding; skip a byte to stay permissive. */
            i++;
        }
    }
    *count_out = n;
    return recs;
}

void dfm_free(DfmComponent **recs, int count)
{
    if (!recs)
        return;
    for (int i = 0; i < count; i++)
        component_free(recs[i]);
    free(recs);
}

DfmValue *dfm_prop(const DfmComponent *c, const char *name)
{
    if (!c)
        return NULL;
    for (int i = 0; i < c->nprops; i++)
        if (strcmp(c->props[i].name, name) == 0)
            return (DfmValue *)&c->props[i].v;
    return NULL;
}

long long dfm_int(const DfmComponent *c, const char *name, long long def)
{
    DfmValue *v = dfm_prop(c, name);
    if (!v)
        return def;
    if (v->type == DFM_INT || v->type == DFM_BOOL)
        return v->i;
    if (v->type == DFM_FLOAT)
        return (long long)v->f;
    return def;
}

bool dfm_bool(const DfmComponent *c, const char *name, bool def)
{
    DfmValue *v = dfm_prop(c, name);
    if (!v)
        return def;
    if (v->type == DFM_BOOL || v->type == DFM_INT)
        return v->i != 0;
    return def;
}

const char *dfm_str(const DfmComponent *c, const char *name, const char *def)
{
    DfmValue *v = dfm_prop(c, name);
    if (!v || v->type != DFM_STR)
        return def;
    return v->s;
}

bool dfm_has(const DfmValue *v, const char *s)
{
    if (!v || v->type != DFM_LIST)
        return false;
    for (int i = 0; i < v->count; i++)
        if (v->items[i].type == DFM_STR && strcmp(v->items[i].s, s) == 0)
            return true;
    return false;
}
