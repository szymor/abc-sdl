#include "scene.h"
#include "cursors_data.h"
#include "font.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int le32(const unsigned char *p)
{
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int le16(const unsigned char *p)
{
    return (int)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static SDL_Cursor *g_cursor_cache[32];
static bool g_fix_content = false;

void scene_set_fix_content(bool enabled)
{
    g_fix_content = enabled;
}

static SDL_Cursor *cursor_get(const char *name)
{
    for (int i = 0; i < abc_cursor_count && i < 32; i++) {
        if (strcasecmp(abc_cursors[i].name, name) != 0)
            continue;
        if (!g_cursor_cache[i]) {
            const AbcCursorDef *d = &abc_cursors[i];
            g_cursor_cache[i] = SDL_CreateCursor(d->data, d->mask, d->w, d->h,
                                                 d->hot_x, d->hot_y);
            if (g_cursor_cache[i] && getenv("ABC_DEBUG"))
                log_info("cursor %s installed", d->name);
        }
        return g_cursor_cache[i];
    }
    return NULL;
}

void scene_shutdown_cursors(void)
{
    for (int i = 0; i < 32; i++) {
        if (g_cursor_cache[i]) {
            SDL_DestroyCursor(g_cursor_cache[i]);
            g_cursor_cache[i] = NULL;
        }
    }
}

static void scene_apply_cursor(Scene *s)
{
    const char *want = NULL;
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (c->cls != CC_CURSORS)
            continue;
        DfmValue *v = dfm_prop(c->src, "Kursory.Strings");
        if (dfm_has(v, "PEDZEL"))
            want = "PEDZEL";
    }
    if (want)
        SDL_SetCursor(cursor_get(want));
    else
        SDL_SetCursor(NULL); /* back to the system arrow */
}

static const unsigned char *bmp_trim(const unsigned char *d, int n, int *out_len)
{
    if (!d || n <= 0)
        return NULL;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') {
        *out_len = n;
        return d;
    }
    if (n >= 12 && memcmp(d, "\x07TBitmap", 8) == 0) {
        int len = le32(d + 8);
        if (len > n - 12 || len <= 0)
            len = n - 12;
        *out_len = len;
        return d + 12;
    }
    if (n >= 6 && d[4] == 'B' && d[5] == 'M') {
        int len = le32(d);
        if (len > n - 4 || len <= 0)
            len = n - 4;
        *out_len = len;
        return d + 4;
    }
    return NULL;
}

static SDL_Surface *bmp_surface(DfmValue *v)
{
    if (!v || v->type != DFM_BIN)
        return NULL;
    int len = 0;
    const unsigned char *p = bmp_trim(v->bin, v->bin_len, &len);
    if (!p)
        return NULL;
    SDL_IOStream *io = SDL_IOFromConstMem(p, (size_t)len);
    if (!io)
        return NULL;
    return SDL_LoadBMP_IO(io, true);
}

static SDL_Texture *texture_from_surface(SDL_Renderer *r, SDL_Surface *surf, int *w, int *h,
                                         bool *magenta, bool key_black, bool key_corner)
{
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    if (!rgba)
        return NULL;
    *w = rgba->w;
    *h = rgba->h;
    *magenta = false;
    uint32_t *px = rgba->pixels;
    int total = rgba->w * rgba->h;
    int stride = rgba->pitch / 4;
    uint32_t corner = px[(rgba->h - 1) * stride]; /* VCL transparent colour */
    if (key_corner) {
        /* Puzzle-piece sheets use the tray colour as the VCL transparent
           colour.  Remove the border-connected pixels only: exact matching
           leaves anti-aliased tray-colour fringes, while removing every
           similar pixel would punch holes in the assembled picture. */
        int cr = (corner >> 16) & 255, cg = (corner >> 8) & 255, cb = corner & 255;
        unsigned char *bg = xcalloc((size_t)total, 1);
        for (int i = 0; i < total; i++) {
            int dr = (int)((px[i] >> 16) & 255) - cr;
            int dg = (int)((px[i] >> 8) & 255) - cg;
            int db = (int)(px[i] & 255) - cb;
            if ((px[i] & 0xFF000000u) &&
                dr * dr + dg * dg + db * db <= 64 * 64)
                bg[i] = 1;
        }
        int *stack = xmalloc((size_t)total * sizeof(int));
        int sp = 0;
        for (int x = 0; x < rgba->w; x++) {
            int p0 = x, p1 = (rgba->h - 1) * stride + x;
            if (bg[p0]) { bg[p0] = 0; px[p0] = 0; stack[sp++] = p0; }
            if (bg[p1]) { bg[p1] = 0; px[p1] = 0; stack[sp++] = p1; }
        }
        for (int y = 0; y < rgba->h; y++) {
            int p0 = y * stride, p1 = y * stride + rgba->w - 1;
            if (bg[p0]) { bg[p0] = 0; px[p0] = 0; stack[sp++] = p0; }
            if (bg[p1]) { bg[p1] = 0; px[p1] = 0; stack[sp++] = p1; }
        }
        while (sp > 0) {
            int p = stack[--sp];
            int x = p % stride, y = p / stride;
            int nb[4] = {x > 0 ? p - 1 : -1, x + 1 < rgba->w ? p + 1 : -1,
                         y > 0 ? p - stride : -1,
                         y + 1 < rgba->h ? p + stride : -1};
            for (int k = 0; k < 4; k++) {
                int q = nb[k];
                if (q >= 0 && bg[q]) {
                    bg[q] = 0;
                    px[q] = 0;
                    stack[sp++] = q;
                }
            }
        }
        free(stack);
        free(bg);
    }
    if (key_black && (corner & 0x00FFFFFFu) == 0) {
        /* Remove only the black backdrop connected to the sprite border.
           Interior pure-black details (dots, outlines) stay opaque. */
        int *stack = xmalloc((size_t)total * sizeof(int));
        int sp = 0;
        for (int x = 0; x < rgba->w; x++) {
            int p0 = x, p1 = (rgba->h - 1) * stride + x;
            if ((px[p0] & 0xFF000000u) && !(px[p0] & 0x00FFFFFFu)) { px[p0] = 0; stack[sp++] = p0; }
            if ((px[p1] & 0xFF000000u) && !(px[p1] & 0x00FFFFFFu)) { px[p1] = 0; stack[sp++] = p1; }
        }
        for (int y = 0; y < rgba->h; y++) {
            int p0 = y * stride, p1 = y * stride + rgba->w - 1;
            if ((px[p0] & 0xFF000000u) && !(px[p0] & 0x00FFFFFFu)) { px[p0] = 0; stack[sp++] = p0; }
            if ((px[p1] & 0xFF000000u) && !(px[p1] & 0x00FFFFFFu)) { px[p1] = 0; stack[sp++] = p1; }
        }
        while (sp > 0) {
            int p = stack[--sp];
            int x = p % stride, y = p / stride;
            int nb[4] = {x > 0 ? p - 1 : -1, x + 1 < rgba->w ? p + 1 : -1,
                         y > 0 ? p - stride : -1,
                         y + 1 < rgba->h ? p + stride : -1};
            for (int k = 0; k < 4; k++) {
                int q = nb[k];
                if (q < 0)
                    continue;
                if ((px[q] & 0xFF000000u) && !(px[q] & 0x00FFFFFFu)) {
                    px[q] = 0;
                    stack[sp++] = q;
                }
            }
        }
        free(stack);
    }
    int mag = 0;
    for (int i = 0; i < total; i++) {
        if ((px[i] & 0x00FFFFFFu) == 0x00FF00FFu)
            mag++;
    }
    if (mag > 0 && mag < total / 2) {
        *magenta = true;
        for (int i = 0; i < total; i++)
            if ((px[i] & 0x00FFFFFFu) == 0x00FF00FFu)
                px[i] = 0;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, rgba);
    SDL_DestroySurface(rgba);
    if (tex)
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

/* Some 1-bpp masks store black in palette slot 0 and white in slot 1;
   others use the usual white=0 / black=1 order.  The opaque part is the
   black silhouette, so the bit meaning has to come from the palette. */
static bool mask_has_black_index0(DfmValue *v)
{
    if (!v || v->type != DFM_BIN)
        return false;
    int len = 0;
    const unsigned char *p = bmp_trim(v->bin, v->bin_len, &len);
    if (!p || len < 62)
        return false;
    int off = le32(p + 10);
    int ncol = le32(p + 46);
    if (ncol == 0)
        ncol = 2;
    if (ncol < 2 || off < 54 + ncol * 4 || 54 + ncol * 4 > len)
        return false;
    const unsigned char *p0 = p + 54;
    const unsigned char *p1 = p + 58;
    bool b0 = p0[0] == 0 && p0[1] == 0 && p0[2] == 0;
    bool w1 = p1[0] == 255 && p1[1] == 255 && p1[2] == 255;
    return b0 && w1;
}

/* Decode a 1-bpp BMP blob to a byte-per-pixel mask.
   flip=true gives screen/top-down rows; flip=false keeps BMP (bottom-up)
   row order, which is what TLabirynt's entrance coordinates use. */
static unsigned char *mask_from_blob(DfmValue *v, int *w, int *h, bool flip, bool *shape)
{
    *w = *h = 0;
    if (shape)
        *shape = false;
    if (!v || v->type != DFM_BIN)
        return NULL;
    int len = 0;
    const unsigned char *p = bmp_trim(v->bin, v->bin_len, &len);
    if (!p || len < 54)
        return NULL;
    int off = le32(p + 10);
    int W = le32(p + 18);
    int H = le32(p + 22);
    int bpp = le16(p + 28);
    if (W <= 0 || H == 0 || bpp != 1)
        return NULL;
    int topdown = H < 0;
    if (H < 0)
        H = -H;
    int stride = ((W + 31) / 32) * 4;
    if (off < 0 || off + stride * H > len)
        return NULL;
    unsigned char *mask = xcalloc((size_t)W * (size_t)H, 1);
    int count = 0;
    int minx = W, miny = H, maxx = -1, maxy = -1;
    for (int y = 0; y < H; y++) {
        int src_y;
        if (flip)
            src_y = topdown ? y : H - 1 - y;
        else
            src_y = y;
        const unsigned char *row = p + off + (size_t)src_y * stride;
        for (int x = 0; x < W; x++) {
            int bit = (row[x / 8] >> (7 - (x % 8))) & 1;
            if (bit) {
                mask[y * W + x] = 1;
                count++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    *w = W;
    *h = H;
    if (shape) {
        double coverage = (double)count / (double)(W * H);
        double bbox = 0;
        if (count)
            bbox = (double)(maxx - minx + 1) * (double)(maxy - miny + 1) / (double)(W * H);
        *shape = (count > 0 && coverage > 0.015 && coverage < 0.85 && bbox < 0.95);
    }
    return mask;
}

static MIX_Audio *wave_blob(DfmValue *v)
{
    if (!v || v->type != DFM_BIN || v->bin_len < 12)
        return NULL;
    if (memcmp(v->bin, "RIFF", 4) != 0)
        return NULL;
    return audio_load_mem(v->bin, v->bin_len);
}

static bool named_color(const char *name, uint32_t *out)
{
    static const struct { const char *n; uint32_t c; } map[] = {
        {"clBlack", 0x000000}, {"clMaroon", 0x800000},
        {"clGreen", 0x008000}, {"clOlive", 0x808000},
        {"clNavy", 0x000080}, {"clPurple", 0x800080},
        {"clTeal", 0x008080}, {"clGray", 0x808080},
        {"clSilver", 0xC0C0C0}, {"clRed", 0xFF0000},
        {"clLime", 0x00FF00}, {"clYellow", 0xFFFF00},
        {"clBlue", 0x0000FF}, {"clFuchsia", 0xFF00FF},
        {"clAqua", 0x00FFFF}, {"clWhite", 0xFFFFFF},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (str_ieq(name, map[i].n)) {
            *out = 0xFF000000u | map[i].c;
            return true;
        }
    }
    return false;
}

static CompClass class_of(const char *cls)
{
    if (strcmp(cls, "TImageHotSpot") == 0) return CC_IMAGE;
    if (strcmp(cls, "TZamienBMP") == 0) return CC_ZAMIEN;
    if (strcmp(cls, "TWysunBMP") == 0) return CC_WYSUN;
    if (strcmp(cls, "THotSpot") == 0) return CC_HOTSPOT;
    if (strcmp(cls, "TAnimujBMP") == 0) return CC_ANIM;
    if (strcmp(cls, "TMapowHotSpot") == 0) return CC_MAPOW;
    if (strcmp(cls, "TMapowanie") == 0) return CC_MAPOWANIE;
    if (strcmp(cls, "TTloMapowanie") == 0) return CC_TLO_MAPOWANIE;
    if (strcmp(cls, "TEkran") == 0) return CC_EKRAN;
    if (strcmp(cls, "TSprawdzanie") == 0) return CC_CHECK;
    if (strcmp(cls, "TLoadCursors") == 0) return CC_CURSORS;
    if (strcmp(cls, "TWavePlayer") == 0) return CC_WAVE;
    if (strcmp(cls, "TRandomMediaPlayer") == 0) return CC_MIDI;
    if (strcmp(cls, "TMediaPlayer") == 0) return CC_MEDIA;
    if (strcmp(cls, "TLabirynt") == 0) return CC_LABIRYNT;
    if (strcmp(cls, "TPrzesunBMP") == 0) return CC_PRZESUN;
    if (strcmp(cls, "TTimer") == 0) return CC_TIMER;
    return CC_UNKNOWN;
}

static int collect_count(DfmComponent *c)
{
    int n = 1;
    for (int i = 0; i < c->nchildren; i++)
        n += collect_count(c->children[i]);
    return n;
}

static void collect_fill(DfmComponent *c, Comp *comps, int *idx)
{
    comps[*idx].src = c;
    (*idx)++;
    for (int i = 0; i < c->nchildren; i++)
        collect_fill(c->children[i], comps, idx);
}

/* Skladanka parts are drawn with a 1-bpp silhouette mask: the bitmap
   often has a black/page-coloured surround that must not be painted. */
static void apply_mask_alpha(Scene *s, Comp *c)
{
    if (!c->tex || !c->mask)
        return;
    DfmValue *bmp = dfm_prop(c->src, "Picture.Data");
    if (!bmp || bmp->type != DFM_BIN)
        bmp = dfm_prop(c->src, "_BMP.Data");
    SDL_Surface *surf = bmp_surface(bmp);
    if (!surf)
        return;
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!rgba)
        return;
    if (rgba->w != c->mask_w || rgba->h != c->mask_h) {
        SDL_DestroySurface(rgba);
        return;
    }
    uint32_t *px = rgba->pixels;
    for (int y = 0; y < rgba->h; y++) {
        for (int x = 0; x < rgba->w; x++) {
            int i = y * rgba->w + x;
            if (!c->mask[i])
                px[y * (rgba->pitch / 4) + x] = 0;
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(s->renderer, rgba);
    SDL_DestroySurface(rgba);
    if (!tex)
        return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_DestroyTexture(c->tex);
    c->tex = tex;
}

/* Locate the visible picture content inside the placed frame.  Puzzle-piece
   sheets have a transparent border in their tray colour; placing the frame
   at the slot corner would shift the content by that border (2-4 px). */
static void compute_piece_content(Comp *c)
{
    DfmValue *bmp = dfm_prop(c->src, "_BMP.Data");
    if (!bmp || bmp->type != DFM_BIN)
        return;
    SDL_Surface *surf = bmp_surface(bmp);
    if (!surf)
        return;
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!rgba)
        return;
    uint32_t *px = rgba->pixels;
    int stride = rgba->pitch / 4;
    int frames = c->frame_count > 0 ? c->frame_count : 1;
    int fw = rgba->w / frames;
    int fi = c->abnormal > 0 ? c->abnormal - 1 : 0;
    if (fi < 0 || fi >= frames)
        fi = 0;
    uint32_t corner = px[(rgba->h - 1) * stride];
    /* The tray-colour border is not perfectly flat: a few anti-aliased
       pixels differ from the corner colour.  Treat a row/column as border
       when most of it is within a small colour distance of that colour, and
       take the first row/column that is not; otherwise one fringe pixel can
       shift the whole piece and leave a background-coloured sliver. */
    int cr = (corner >> 16) & 255, cg = (corner >> 8) & 255, cb = corner & 255;
    int minx = fw, miny = rgba->h;
    for (int y = 0; y < rgba->h; y++) {
        int near = 0;
        for (int x = fi * fw; x < (fi + 1) * fw; x++) {
            uint32_t p = px[y * stride + x];
            int dr = (int)((p >> 16) & 255) - cr;
            int dg = (int)((p >> 8) & 255) - cg;
            int db = (int)(p & 255) - cb;
            if (!(p & 0xFF000000u) || dr * dr + dg * dg + db * db <= 30 * 30)
                near++;
        }
        if (near * 2 < fw) { miny = y; break; }
    }
    for (int x = 0; x < fw; x++) {
        int near = 0;
        for (int y = 0; y < rgba->h; y++) {
            uint32_t p = px[y * stride + fi * fw + x];
            int dr = (int)((p >> 16) & 255) - cr;
            int dg = (int)((p >> 8) & 255) - cg;
            int db = (int)(p & 255) - cb;
            if (!(p & 0xFF000000u) || dr * dr + dg * dg + db * db <= 30 * 30)
                near++;
        }
        if (near * 2 < rgba->h) { minx = x; break; }
    }
    if (minx == fw && miny == rgba->h) {
        c->content_off_x = 0;
        c->content_off_y = 0;
    } else {
        c->content_off_x = minx;
        c->content_off_y = miny;
    }
    SDL_DestroySurface(rgba);
}

static void load_comp(Scene *s, Comp *c)
{
    DfmComponent *src = c->src;
    snprintf(c->name, sizeof(c->name), "%s", src->name ? src->name : "");
    c->cls = class_of(src->cls ? src->cls : "");

    c->left = (int)dfm_int(src, "Left", 0);
    c->top = (int)dfm_int(src, "Top", 0);
    c->width = (int)dfm_int(src, "Width", 0);
    c->height = (int)dfm_int(src, "Height", 0);
    c->visible = dfm_bool(src, "Visible", true);
    c->visible0 = c->visible;
    c->enabled = dfm_bool(src, "Enabled", true);
    c->autosize = dfm_bool(src, "_AutoSize", false);
    c->align_client = str_ieq(dfm_str(src, "Align", ""), "alClient");
    c->home_left = c->left;
    c->home_top = c->top;

    c->normal = (int)dfm_int(src, "_BMPNumerNormal", 0);
    c->abnormal = (int)dfm_int(src, "_BMPNumerAbnormal", 0);
    c->special = (int)dfm_int(src, "_BMPNumerSpecial", 0);
    c->frame_count = (int)dfm_int(src, "_BMPIlosc", 0);
    if (c->frame_count < 1)
        c->frame_count = 1;
    c->state = ST_NORMAL;

    c->has_color = false;
    c->color = 0;
    {
        DfmValue *cv = dfm_prop(src, "Color");
        if (cv && cv->type == DFM_STR) {
            uint32_t col;
            if (named_color(cv->s, &col)) {
                c->has_color = true;
                c->color = col;
            }
        } else if (cv && (cv->type == DFM_INT || cv->type == DFM_FLOAT)) {
            uint32_t v = (uint32_t)dfm_int(src, "Color", 0);
            /* TColor is 0x00BBGGRR. */
            c->has_color = true;
            c->color = 0xFF000000u | ((v & 0xFF) << 16) |
                       (((v >> 8) & 0xFF) << 8) | ((v >> 16) & 0xFF);
        }
    }

    c->modal_set = false;
    {
        DfmValue *mv = dfm_prop(src, "_ModalResult");
        if (mv && mv->type == DFM_INT && mv->i != 0) {
            c->modal_set = true;
            c->modal_result = (int)mv->i;
        }
    }
    c->auto_click_exit = dfm_bool(src, "_AutoClickExit", false);
    c->click_to_abnormal = dfm_has(dfm_prop(src, "_AutoClickChange"), "toAbnormal");
    c->click_to_normal = dfm_has(dfm_prop(src, "_AutoClickChange"), "toNormal");
    c->move_to_abnormal = dfm_has(dfm_prop(src, "_AutoMoveChange"), "toAbnormal");
    c->move_to_normal = dfm_has(dfm_prop(src, "_AutoMoveChange"), "toNormal");
    c->autoplay_normal = dfm_has(dfm_prop(src, "_WaveAutoPlay"), "fromNormal");
    c->autoplay_abnormal = dfm_has(dfm_prop(src, "_WaveAutoPlay"), "fromAbnormal");
    c->wave_stop = dfm_bool(src, "_WaveStop", false);
    c->wave_auto_click = dfm_bool(src, "_WaveAutoClick", false);
    c->mask_mouse = dfm_bool(src, "_BMPMapMouse", false) || dfm_bool(src, "_BMPMapMouseConst", false);
    c->mask_graph = dfm_bool(src, "_BMPMapGraph", false) || dfm_bool(src, "_BMPMapGraphConst", false);

    const char *cf = dfm_str(src, "_WaveClickFile", NULL);
    const char *cf2 = dfm_str(src, "_WaveClickFile2", NULL);
    if (cf) snprintf(c->click_file, sizeof(c->click_file), "%s", cf);
    if (cf2) snprintf(c->click_file2, sizeof(c->click_file2), "%s", cf2);

    /* Bitmap (background or sprite). */
    DfmValue *bmp = dfm_prop(src, "Picture.Data");
    if (!bmp || bmp->type != DFM_BIN)
        bmp = dfm_prop(src, "_BMP.Data");
    SDL_Surface *surf = bmp_surface(bmp);
    if (surf) {
        int tw = 0, th = 0;
        bool mag = false;
        /* Animations have a legacy black backdrop; crayons instead have a
           precise silhouette mask (the black graphite tip and dots are part
           of the artwork, so a colour key would erase them). */
        bool key_black = (c->cls == CC_ANIM);
        c->key_corner = (s->module == 11 && c->cls == CC_ZAMIEN &&
                         c->name[0] == 'k' && isdigit((unsigned char)c->name[1]));
        c->tex = texture_from_surface(s->renderer, surf, &tw, &th, &mag,
                                      key_black, c->key_corner);
        if (c->tex && c->key_corner) {
            compute_piece_content(c);
            if (getenv("ABC_DEBUG"))
                fprintf(stderr, "puzzle %s content offset %d,%d\n", c->name,
                        c->content_off_x, c->content_off_y);
        }
        SDL_DestroySurface(surf);
        if (c->tex) {
            c->tex_w = tw;
            c->tex_h = th;
            c->key_magenta = mag;
            c->frame_w = (c->frame_count > 0) ? tw / c->frame_count : tw;
            if (c->frame_w <= 0)
                c->frame_w = tw;
            c->frame_h = th;
            if (c->align_client && getenv("ABC_DEBUG"))
                fprintf(stderr, "align %s native %dx%d\n", c->name,
                        c->frame_w, c->frame_h);
        }
    }

    /* Masks. */
    DfmValue *maskv = dfm_prop(src, "_BMPMap.Data");
    if (!maskv)
        maskv = dfm_prop(src, "_BMPMapConst.Data");
    if (maskv) {
        bool shape = false;
        c->mask = mask_from_blob(maskv, &c->mask_w, &c->mask_h, true, &shape);
        c->mask_shape = shape;
        /* TMapowanie masks normally use palette 0=white / 1=black, but a
           few (e.g. AUTO's m4 mirrors) store the palette the other way. */
        if (c->cls == CC_MAPOWANIE && c->mask && mask_has_black_index0(maskv)) {
            for (int i = 0; i < c->mask_w * c->mask_h; i++)
                c->mask[i] ^= 1;
        }
    }
    if (c->cls == CC_MAPOWANIE || c->cls == CC_PRZESUN ||
        c->cls == CC_WYSUN || c->mask_shape)
        apply_mask_alpha(s, c);

    /* Embedded WAVs. */
    c->wav_normal = wave_blob(dfm_prop(src, "_WaveNormal.Data"));
    c->wav_abnormal = wave_blob(dfm_prop(src, "_WaveAbnormal.Data"));
    if (c->cls == CC_WAVE) {
        MIX_Audio *w = wave_blob(dfm_prop(src, "Wave.Data"));
        if (w) {
            if (c->wav_normal)
                audio_free_mem(c->wav_normal);
            c->wav_normal = w;
        }
    }

    /* Click sounds from WAV/. Resolve lazily but check existence. */
    if (c->click_file[0]) {
        char *p = resolve_asset(s->data_root, c->click_file);
        if (file_exists(p))
            c->wav_click = audio_load_file(p);
        free(p);
    }
    if (c->click_file2[0]) {
        char *p = resolve_asset(s->data_root, c->click_file2);
        if (file_exists(p))
            c->wav_click2 = audio_load_file(p);
        free(p);
    }

    if (c->cls == CC_ANIM) {
        c->is_anim = true;
        c->interval = (int)dfm_int(src, "Interval", 500);
        if (c->interval < 20)
            c->interval = 20;
        c->loop_anim = dfm_bool(src, "_Loop", false);
        DfmValue *kl = dfm_prop(src, "_Klatki.Strings");
        if (kl && kl->type == DFM_LIST) {
            for (int i = 0; i < kl->count && c->nklatki < 63; i++) {
                if (kl->items[i].type == DFM_STR)
                    c->klatki[c->nklatki++] = atoi(kl->items[i].s);
            }
        }
        if (c->nklatki == 0) {
            c->klatki[0] = 1;
            c->klatki[1] = 0;
            c->nklatki = 2;
        }
    }

    if (c->cls == CC_WYSUN) {
        c->slide_max = (int)dfm_int(src, "_DoKiedyWsowac", 46);
        c->slide_speed = (int)dfm_int(src, "_Skok", 20);
        if (c->slide_max < 40)
            c->slide_max = 140;
    }

    if (c->cls == CC_LABIRYNT) {
        c->max_speed = (int)dfm_int(src, "_MaxPredkosc", 20);
        c->entry_x = (int)dfm_int(src, "wejscieX", 17);
        c->entry_y = (int)dfm_int(src, "wejscieY", 299);
        DfmValue *col = dfm_prop(src, "_BMPMapConst.Data");
        if (col) {
            bool shape = false;
            /* Bottom-up row order matches the scene's wejscieX/Y values. */
            c->collision = mask_from_blob(col, &c->col_w, &c->col_h, false, &shape);
        }
    }

    if (c->cls == CC_PRZESUN) {
        /* Compute wagon boxes from the link mask: 25 content runs,
           first is the locomotive, next 24 are letters A..Z. */
        c->box_count = 0;
        if (c->mask) {
            int thresh = (c->mask_h * 28) / 100;
            int start = -1;
            int runs[64][2];
            int nruns = 0;
            for (int x = 0; x < c->mask_w && nruns < 64; x++) {
                int count = 0;
                for (int y = 0; y < c->mask_h; y++)
                    count += c->mask[y * c->mask_w + x];
                bool on = count > thresh;
                if (on && start < 0)
                    start = x;
                if (!on && start >= 0) {
                    if (x - start >= 30) {
                        runs[nruns][0] = start;
                        runs[nruns][1] = x - 1;
                        nruns++;
                    }
                    start = -1;
                }
            }
            if (start >= 0 && nruns < 64 && c->mask_w - start >= 30) {
                runs[nruns][0] = start;
                runs[nruns][1] = c->mask_w - 1;
                nruns++;
            }
            int first = (nruns == 25) ? 1 : 0; /* skip locomotive when clean */
            for (int i = first; i < nruns && c->box_count < 24; i++) {
                c->boxes[c->box_count].x = runs[i][0];
                c->boxes[c->box_count].y = 0;
                c->boxes[c->box_count].w = runs[i][1] - runs[i][0] + 1;
                c->boxes[c->box_count].h = c->mask_h;
                c->box_targets[c->box_count] = 2001 + c->box_count;
                c->box_count++;
            }
            if (c->box_count == 24) {
                DfmValue *t = dfm_prop(src, "_OblModalResult.Strings");
                if (t && t->type == DFM_LIST) {
                    for (int i = 0; i < t->count && i < 24; i++)
                        if (t->items[i].type == DFM_STR)
                            c->box_targets[i] = atoi(t->items[i].s);
                }
            }
        }
        if (getenv("ABC_DEBUG")) {
            fprintf(stderr, "train boxes=%d:", c->box_count);
            for (int i = 0; i < c->box_count; i++)
                fprintf(stderr, " %d[%d+%d]", i, c->boxes[i].x, c->boxes[i].w);
            fprintf(stderr, "\n");
        }
        if (c->box_count < 24) {
            /* Calibrated from the 4050x145 strip artwork: the locomotive
               occupies 0..217, then 24 wagons at a ~157 px pitch. */
            int x0 = 243;
            int pitch = 157;
            int wid = 152;
            c->box_count = 24;
            for (int i = 0; i < 24; i++) {
                c->boxes[i].x = x0 + i * pitch;
                c->boxes[i].y = 0;
                c->boxes[i].w = wid;
                c->boxes[i].h = c->height;
                c->box_targets[i] = 2001 + i;
            }
        }
    }
}

Scene *scene_load(SDL_Renderer *r, const char *data_root, int id)
{
    char rel[64];
    snprintf(rel, sizeof(rel), "frm/%d.FRM", id);
    char *path = resolve_asset(data_root, rel);
    size_t len = 0;
    unsigned char *data = read_file(path, &len);
    if (!data) {
        log_error("scene %d: cannot read %s", id, path);
        free(path);
        return NULL;
    }
    free(path);

    int nroots = 0;
    DfmComponent **roots = dfm_parse(data, len, &nroots);
    if (nroots == 0) {
        log_error("scene %d: no DFM records", id);
        free(data);
        dfm_free(roots, nroots);
        return NULL;
    }

    Scene *s = xcalloc(1, sizeof(*s));
    s->renderer = r;
    s->id = id;
    s->module = id / 1000;
    snprintf(s->data_root, sizeof(s->data_root), "%s", data_root);
    s->roots = roots;
    s->nroots = nroots;
    s->return_id = 3000 + (id % 1000);
    s->complete_delay_ms = 1600;
    s->request_id = -1;
    s->toolbar_anim = 0.0f;
    s->sel_color = 0;
    s->selected_idx = -1;
    s->drag_idx = -1;

    int total = 0;
    for (int i = 0; i < nroots; i++)
        total += collect_count(roots[i]);
    s->comps = xcalloc((size_t)total, sizeof(Comp));
    int idx = 0;
    for (int i = 0; i < nroots; i++)
        collect_fill(roots[i], s->comps, &idx);
    s->ncomps = idx;

    if (s->module == 0) {
        s->origin_x = (ABC_W - 482) / 2;
        s->origin_y = (ABC_H - 476) / 2;
    }
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "scene %d origin %d,%d\n", id, s->origin_x, s->origin_y);

    s->now = SDL_GetTicks();
    s->start_time = s->now;
    for (int i = 0; i < s->ncomps; i++)
        load_comp(s, &s->comps[i]);

    /* YETI is the only Y word (scene 3135), but its two word arrows point
       back at 3135 itself.  Point them at the Y letter playground instead.
       The six unused Y hotspots in 2023 fall back to unrelated A words;
       hide them.  This is the minimal YETI navigation fix. */
    if (id == 3135) {
        /* YETI is the only Y word.  The shipped data points both word arrows
           back at 3135, so they are dead controls; keep them inactive rather
           than inventing a destination.  Their artwork stays in tlo. */
        Comp *l = scene_find(s, "browWyrL");
        Comp *r = scene_find(s, "browWyrP");
        if (l) {
            l->visible = l->visible0 = false;
            l->modal_set = false;
            l->auto_click_exit = false;
            l->hover_lock = true;
        }
        if (r) {
            r->visible = r->visible0 = false;
            r->modal_set = false;
            r->auto_click_exit = false;
            r->hover_lock = true;
        }
        if (getenv("ABC_DEBUG"))
            log_info("YETI word arrows disabled");
    } else if (id == 2023) {
        for (int i = 2; i <= 7; i++) {
            char nm[8];
            snprintf(nm, sizeof(nm), "Z%d", i);
            Comp *c = scene_find(s, nm);
            if (!c)
                continue;
            c->visible = false;
            c->visible0 = false;
            c->modal_set = false;
            c->auto_click_exit = false;
            c->hover_lock = true;
        }
    }

    /* Opt-in content fixes. The shipped data leaves IGLO (3046) outside the
       I arrow cycle because 3047 points left at 3050 and 3050 points right
       back at 3047. Re-link those two arrows so the cycle includes IGLO. */
    if (g_fix_content) {
        if (id == 3047) {
            Comp *l = scene_find(s, "browWyrL");
            if (l) { l->modal_set = true; l->modal_result = 3046; }
        } else if (id == 3050) {
            Comp *r = scene_find(s, "browWyrP");
            if (r) { r->modal_set = true; r->modal_result = 3046; }
        }
    }

    modules_setup(s, data_root);
    scene_apply_cursor(s);

    /* The original introduces an activity with its spoken instruction; the
       HELP pencil replays the same cue.  The per-scene cue lives on kHelp's
       _WaveClickFile (WAV/01.WAV, 02.WAV, …). */
    if (s->module >= 10 && s->module <= 18) {
        Comp *help = scene_find(s, "kHelp");
        if (getenv("ABC_DEBUG"))
            fprintf(stderr, "entry help %d: %s\n", s->id,
                    (help && help->wav_click) ? "audio" : "silent");
        if (help && help->wav_click)
            audio_play(help->wav_click, 1.0f);
    }
    free(data);
    log_info("scene %d loaded (%d components)", id, s->ncomps);
    return s;
}

static void comp_unload(Comp *c)
{
    if (c->tex)
        SDL_DestroyTexture(c->tex);
    free(c->mask);
    free(c->collision);
    if (c->wav_normal) audio_free_mem(c->wav_normal);
    if (c->wav_abnormal) audio_free_mem(c->wav_abnormal);
    /* wav_click / wav_click2 are owned by the audio cache. */
}

void scene_free(Scene *s)
{
    if (!s)
        return;
    /* Do not let one-shots (word pronunciation, help, video sound) leak into
       the next scene.  MIDI music is managed separately. */
    audio_stop_all();
    for (int i = 0; i < s->ncomps; i++)
        comp_unload(&s->comps[i]);
    free(s->comps);
    dfm_free(s->roots, s->nroots);
    /* Embedded waves are owned by their components; file waves by the cache. */
    if (s->paint_tex)
        SDL_DestroyTexture(s->paint_tex);
    if (s->video)
        video_close(s->video);
    free(s->paint_bg);
    free(s->paint_px);
    free(s);
}

Comp *scene_find(Scene *s, const char *name)
{
    for (int i = 0; i < s->ncomps; i++)
        if (str_ieq(s->comps[i].name, name))
            return &s->comps[i];
    return NULL;
}

Comp *scene_find_prefix(Scene *s, const char *prefix)
{
    size_t n = strlen(prefix);
    for (int i = 0; i < s->ncomps; i++)
        if (strncasecmp(s->comps[i].name, prefix, n) == 0)
            return &s->comps[i];
    return NULL;
}

static int comp_frame_selector(Comp *c)
{
    if (c->is_anim && c->anim_playing) {
        if (c->anim_index >= 0 && c->anim_index < c->nklatki)
            return c->klatki[c->anim_index];
        return -2;
    }
    switch (c->state) {
    case ST_NORMAL: return c->normal;
    case ST_ABNORMAL: return c->abnormal;
    case ST_SPECIAL: return c->special;
    }
    return c->normal;
}

void comp_rect(Scene *s, Comp *c, SDL_FRect *out)
{
    float x = (float)(s->origin_x + c->left);
    float y = (float)(s->origin_y + c->top);
    float w = (float)c->width;
    float h = (float)c->height;
    if ((c->autosize || c->align_client) && c->tex) {
        w = (float)c->frame_w;
        h = (float)c->frame_h;
    }
    if (c->cls == CC_WYSUN && s->has_toolbar) {
        float hide = (1.0f - s->toolbar_anim) * 140.0f;
        y += hide;
    }
    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
}

bool comp_contains(Scene *s, Comp *c, int x, int y)
{
    SDL_FRect r;
    comp_rect(s, c, &r);
    if (!c->visible)
        return false;
    if ((float)x < r.x || (float)y < r.y || (float)x >= r.x + r.w || (float)y >= r.y + r.h)
        return false;
    if (c->mask && c->mask_shape && c->mask_mouse) {
        int sel = comp_frame_selector(c);
        int frame = sel <= 0 ? 0 : sel - 1;
        int mx = (int)((float)x - (s->origin_x + c->left));
        int my = (int)((float)y - (s->origin_y + c->top));
        if (c->tex && c->frame_w > 0)
            mx += frame * c->frame_w;
        if (mx >= 0 && my >= 0 && mx < c->mask_w && my < c->mask_h)
            return c->mask[my * c->mask_w + mx] != 0;
        return false;
    }
    return true;
}

static bool is_interactive(Comp *c)
{
    switch (c->cls) {
    case CC_ZAMIEN:
    case CC_WYSUN:
    case CC_HOTSPOT:
        return true;
    case CC_IMAGE:
        return c->modal_set || c->click_file[0] || c->click_file2[0];
    default:
        return false;
    }
}

int scene_hit_index(Scene *s, int x, int y)
{
    for (int i = s->ncomps - 1; i >= 0; i--) {
        Comp *c = &s->comps[i];
        if (!is_interactive(c))
            continue;
        if (comp_contains(s, c, x, y))
            return i;
    }
    return -1;
}

Comp *scene_hit(Scene *s, int x, int y)
{
    int i = scene_hit_index(s, x, y);
    return i >= 0 ? &s->comps[i] : NULL;
}

static void play_state_wave(Scene *s, Comp *c, CompState st)
{
    if (st == ST_ABNORMAL && c->autoplay_abnormal && c->wav_abnormal)
        audio_play(c->wav_abnormal, 1.0f);
    else if (st == ST_NORMAL && c->autoplay_normal && c->wav_normal)
        audio_play(c->wav_normal, 1.0f);
}

void comp_set_state(Scene *s, Comp *c, CompState st)
{
    if (c->state == st)
        return;
    c->state = st;
    if (c->cls == CC_WYSUN) {
        /* Pencils do speak on hover, but only after a short dwell: a pointer
           passing through (or already resting over the toolbar when a game
           opens) should not cut the spoken task instruction by accident. */
        if (st == ST_ABNORMAL && c->autoplay_abnormal && c->wav_abnormal) {
            c->hover_wave_pending = true;
            c->hover_wave_at = SDL_GetTicks() + 250;
        } else {
            c->hover_wave_pending = false;
        }
    } else {
        play_state_wave(s, c, st);
    }
}

void comp_play_click(Scene *s, Comp *c)
{
    MIX_Audio *a = NULL;
    if (c->state == ST_ABNORMAL) {
        /* Hover-only components (e.g. the HELP pencil) are already in the
           abnormal state when clicked.  They still use their normal click
           file, not the generic/default player. */
        if (c->wav_click2)
            a = c->wav_click2;
        else if (c->wav_click)
            a = c->wav_click;
        else if (c->wav_abnormal)
            a = c->wav_abnormal;
        else if (c->wav_normal)
            a = c->wav_normal;
    } else {
        if (c->wav_click)
            a = c->wav_click;
        else if (c->wav_abnormal)
            a = c->wav_abnormal;
        else if (c->wav_normal)
            a = c->wav_normal;
    }
    c->hover_wave_pending = false;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "click %-12s state=%d audio=%s\n", c->name,
                (int)c->state, a ? "yes" : (s->wav_default ? "default" : "none"));
    /* A help cue may be several seconds long: restart it instead of layering
       another copy on top of the one already playing. */
    if (str_ieq(c->name, "kHelp"))
        audio_stop_all();
    if (a)
        audio_play(a, 1.0f);
    else if (s->wav_default)
        audio_play(s->wav_default, 1.0f);
}

void comp_anim_start(Scene *s, Comp *c, bool show)
{
    if (!c->is_anim)
        return;
    if (show)
        c->visible = true;
    c->anim_index = 0;
    c->anim_playing = true;
    c->anim_next = SDL_GetTicks() + (uint64_t)c->interval;
}

void scene_request(Scene *s, int id)
{
    if (!s->request_pending) {
        s->request_pending = true;
        s->request_id = id;
    }
}

void scene_complete(Scene *s, int delay_ms)
{
    if (s->complete)
        return;
    s->complete = true;
    s->complete_at = SDL_GetTicks();
    s->complete_delay_ms = delay_ms;
    /* Puzzle's _WaveLicznik is the long spoken congratulation; it belongs
       at the end, not on each placed piece. */
    if (s->module == 11 && s->wav_count)
        audio_play(s->wav_count, 1.0f);
    else if (s->wav_ok)
        audio_play(s->wav_ok, 1.0f);
    else if (s->wav_count)
        audio_play(s->wav_count, 1.0f);
}

void scene_toast(Scene *s, const char *msg, int ms)
{
    snprintf(s->toast, sizeof(s->toast), "%s", msg ? msg : "");
    s->toast_until = SDL_GetTicks() + (uint64_t)ms;
}

void scene_set_player(Scene *s, int x, int y)
{
    s->player_x = x;
    s->player_y = y;
    s->player_init = true;
}

void scene_mouse_down(Scene *s, int x, int y)
{
    s->mouse_down = true;
    s->drag_idx = -1;
    s->drag_moved = false;
    modules_mouse_down(s, x, y);
}

void scene_mouse_up(Scene *s, int x, int y)
{
    modules_mouse_up(s, x, y);
    s->mouse_down = false;
    s->drag_idx = -1;
    s->drag_moved = false;
}

SDL_Surface *scene_bmp_surface(DfmValue *v)
{
    return bmp_surface(v);
}

uint32_t scene_sample_bmp(DfmComponent *src)
{
    if (!src)
        return 0;
    DfmValue *v = dfm_prop(src, "_BMP.Data");
    if (!v)
        v = dfm_prop(src, "Picture.Data");
    SDL_Surface *surf = bmp_surface(v);
    if (!surf)
        return 0;
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!rgba)
        return 0;
    int frames = (int)dfm_int(src, "_BMPIlosc", 1);
    if (frames < 1)
        frames = 1;
    int fw = rgba->w / frames;
    /* Palette swatches are flat colour chips with a black outline.  Sample
       the middle of the first frame; the old five-point/brightest-pixel
       heuristic picked highlights or the pale tray background and made many
       colours paint white. */
    int x = fw / 2;
    int y = rgba->h / 2;
    uint32_t best = ((uint32_t *)rgba->pixels)[y * (rgba->pitch / 4) + x];
    SDL_DestroySurface(rgba);
    return best | 0xFF000000u;
}

static void hover_update(Scene *s)
{
    int hit = scene_hit_index(s, s->mouse_x, s->mouse_y);
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (c->hover_lock)
            continue;
        bool over = (i == hit);
        if (over && c->move_to_abnormal && c->state == ST_NORMAL)
            comp_set_state(s, c, ST_ABNORMAL);
        else if (!over && c->move_to_normal && c->state == ST_ABNORMAL)
            comp_set_state(s, c, ST_NORMAL);
    }
}

static void hover_wave_update(Scene *s)
{
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (!c->hover_wave_pending)
            continue;
        if (c->state != ST_ABNORMAL) {
            c->hover_wave_pending = false;
            continue;
        }
        if (s->now >= c->hover_wave_at) {
            c->hover_wave_pending = false;
            if (c->wav_abnormal)
                audio_play(c->wav_abnormal, 1.0f);
        }
    }
}

static void anim_update(Scene *s)
{
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (!c->is_anim || !c->anim_playing)
            continue;
        if (s->now < c->anim_next)
            continue;
        c->anim_index++;
        if (c->anim_index >= c->nklatki || c->klatki[c->anim_index] == 0) {
            if (c->loop_anim) {
                c->anim_index = 0;
            } else {
                c->anim_playing = false;
                if (str_ieq(c->name, "AnimZle"))
                    c->visible = false;
            }
        }
        c->anim_next = s->now + (uint64_t)c->interval;
    }
}

static void release_media_buttons(Scene *s)
{
    if (s->module != 3)
        return;
    static const char *buttons[] = {"KlTabl", "KlKamera", "KlAparat", "KlGra"};
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        Comp *c = scene_find(s, buttons[i]);
        if (c && c->state != ST_NORMAL) {
            c->state = ST_NORMAL;
            changed = true;
        }
    }
    if (changed && getenv("ABC_DEBUG"))
        log_info("TV media buttons released after playback");
}

static void restart_scene_music(Scene *s)
{
    if (!s->video_stop_music)
        return;
    s->video_stop_music = false;
    /* Resume the playlist at the point where the clip interrupted it. */
    if (audio_music_resume()) {
        if (getenv("ABC_DEBUG"))
            log_info("video ended: background music restored");
        return;
    }
    audio_music_stop();
    if (s->nmusic <= 0)
        return;
    const char *paths[4];
    for (int i = 0; i < s->nmusic; i++)
        paths[i] = s->music_paths[i];
    audio_music_play(paths, s->nmusic, true, s->music_sig);
    if (getenv("ABC_DEBUG"))
        log_info("video ended: background music restored (restarted)");
}

void scene_update(Scene *s, float dt)
{
    s->now = SDL_GetTicks();
    hover_update(s);
    hover_wave_update(s);
    anim_update(s);

    if (s->has_toolbar) {
        bool near = (s->mouse_x > 430 && s->mouse_y > 300);
        Comp *hit = scene_hit(s, s->mouse_x, s->mouse_y);
        if (hit && hit->cls == CC_WYSUN)
            near = true;
        float target = near ? 1.0f : 0.0f;
        float speed = 9.0f;
        if (s->toolbar_anim < target) {
            s->toolbar_anim += speed * dt;
            if (s->toolbar_anim > target) s->toolbar_anim = target;
        } else if (s->toolbar_anim > target) {
            s->toolbar_anim -= speed * dt;
            if (s->toolbar_anim < target) s->toolbar_anim = target;
        }
    }

    modules_update(s, dt);

    if (s->video && !video_tick(s->video, s->now)) {
        video_close(s->video);
        s->video = NULL;
        release_media_buttons(s);
        restart_scene_music(s);
    }
    if (s->video_until && s->now >= s->video_until) {
        s->video_until = 0;
        release_media_buttons(s);
    }
    if (s->toast_until && s->now >= s->toast_until)
        s->toast_until = 0;

    if (s->paint_dirty && s->paint_tex) {
        SDL_UpdateTexture(s->paint_tex, NULL, s->paint_px, ABC_W * (int)sizeof(uint32_t));
        s->paint_dirty = false;
    }
}

static bool comp_drawable(Comp *c)
{
    switch (c->cls) {
    case CC_IMAGE:
    case CC_ZAMIEN:
    case CC_WYSUN:
    case CC_ANIM:
    case CC_MAPOWANIE:
    case CC_PRZESUN:
        return true;
    default:
        return false;
    }
}

static void render_comp(Scene *s, Comp *c)
{
    if (!comp_drawable(c))
        return;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "draw %-12s vis=%d tex=%p state=%d sel=%d\n", c->name,
                (int)c->visible, (void *)c->tex, (int)c->state, comp_frame_selector(c));
    if (!c->visible)
        return;
    if (c->baked_overlay)
        return;
    if (!c->tex) {
        /* Components without a bitmap can still have a VCL Color; the puzzle
           slots (p01…p14) are red rectangles. */
        if (c->has_color && c->cls == CC_ZAMIEN && s->module == 11 &&
            c->name[0] == 'p' && isdigit((unsigned char)c->name[1])) {
            SDL_FRect r;
            comp_rect(s, c, &r);
            /* The red puzzle cells have black grid lines around them. */
            SDL_FRect border = {r.x - 2, r.y - 2, r.w + 4, r.h + 4};
            SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(s->renderer, &border);
            SDL_SetRenderDrawColor(s->renderer, (c->color >> 16) & 0xFF,
                                   (c->color >> 8) & 0xFF, c->color & 0xFF, 255);
            SDL_RenderFillRect(s->renderer, &r);
        }
        return;
    }
    int sel = comp_frame_selector(c);
    if (c->hover_only) {
        /* Word-screen overlays live above the baked `tlo` art and are shown
           only while hovered.  Their normal frame is the highlight image. */
        if (c->state != ST_ABNORMAL)
            return;
        sel = c->normal;
    }
    if (sel < 0)
        return;
    int frame = sel == 0 ? 1 : sel;
    if (frame > c->frame_count)
        frame = c->frame_count;
    if (frame < 1)
        return;
    SDL_FRect dst;
    comp_rect(s, c, &dst);
    SDL_FRect src;
    src.x = (float)((frame - 1) * c->frame_w);
    src.y = 0;
    src.w = (float)c->frame_w;
    src.h = (float)c->frame_h;
    if (c->cls == CC_PRZESUN) {
        src.x = (float)s->scroll;
        src.w = (float)c->width;
        src.h = (float)c->height;
        dst.w = (float)c->width;
        dst.h = (float)c->height;
    }
    bool clip = (s->module == 11 && c->placed && c->slot_w > 0 && c->slot_h > 0);
    if (clip) {
        SDL_Rect cr = {(int)dst.x + c->content_off_x,
                       (int)dst.y + c->content_off_y,
                       c->slot_w, c->slot_h};
        SDL_SetRenderClipRect(s->renderer, &cr);
    }
    SDL_RenderTexture(s->renderer, c->tex, &src, &dst);
    if (clip)
        SDL_SetRenderClipRect(s->renderer, NULL);
}

void scene_render(Scene *s)
{
    SDL_Renderer *r = s->renderer;
    if (s->module == 10 && s->paint_tex) {
        SDL_FRect dst = {0, 0, ABC_W, ABC_H};
        SDL_RenderTexture(r, s->paint_tex, NULL, &dst);
    }
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (s->module == 10 && c->cls == CC_IMAGE)
            continue; /* replaced by the paint canvas */
        if (s->module == 11 && c->has_color && c->cls == CC_ZAMIEN &&
            c->name[0] == 'p' && isdigit((unsigned char)c->name[1])) {
            /* Once the matching piece is placed, drop the red cell and its
               grid border so the picture shows through around the piece. */
            int idx = atoi(c->name + 1);
            char kn[8];
            snprintf(kn, sizeof(kn), "k%02d", idx);
            Comp *k = scene_find(s, kn);
            if (k && k->placed)
                continue;
        }
        render_comp(s, c);
    }

    if (s->module == 14 && s->player_init) {
        /* The original installs a letter cursor (LIT_C…) for the maze word.
           We draw the word's initial as a sprite over the pointer position. */
        const char *title = scene_word_title(s->id % 1000);
        char letter[2] = {title && title[0] ? title[0] : '?', 0};
        int tw = font_text_width(letter, 3);
        int th = font_text_height(3);
        int tx = s->player_x - tw / 2;
        int ty = s->player_y - th / 2;
        font_draw(r, (float)tx + 2, (float)ty + 2, letter, 3, 0, 0, 0);
        font_draw(r, (float)tx, (float)ty, letter, 3, 255, 235, 40);
    }

    modules_render_overlay(s);

    if (s->video) {
        SDL_Rect vr;
        if (s->video_fullscreen)
            vr = (SDL_Rect){0, 0, ABC_W, ABC_H};
        else
            vr = s->video_rect;
        video_render(s->video, r, vr);
    } else if (s->video_until) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_FRect vrect = {(float)s->video_rect.x, (float)s->video_rect.y,
                           (float)s->video_rect.w, (float)s->video_rect.h};
        SDL_RenderFillRect(r, &vrect);
        SDL_SetRenderDrawColor(r, 30, 120, 60, 255);
        SDL_FRect inner = {s->video_rect.x + 3, s->video_rect.y + 3,
                           s->video_rect.w - 6, s->video_rect.h - 6};
        SDL_RenderRect(r, &inner);
        font_draw(r, s->video_rect.x + 10, s->video_rect.y + 12, s->video_label, 2, 140, 255, 175);
        font_draw(r, s->video_rect.x + 10, s->video_rect.y + 40, "WIDEO W PRZYGOTOWANIU", 1, 120, 220, 150);
    }

    if (s->info_overlay) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 235);
        SDL_FRect all = {0, 0, ABC_W, ABC_H};
        SDL_RenderFillRect(r, &all);
        font_draw(r, 190, 170, "MOJE PIERWSZE ABC", 3, 255, 210, 40);
        font_draw(r, 210, 230, "1996 OPTIMUS NEXUS", 2, 220, 220, 220);
        font_draw(r, 225, 270, "AUTORZY: WIT I LUK", 2, 220, 220, 220);
        font_draw(r, 190, 340, "KLIKNIJ ABY POWROCIC", 1, 150, 150, 150);
    }

    if (s->toast_until) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 210);
        SDL_FRect t = {40, 420, 560, 36};
        SDL_RenderFillRect(r, &t);
        font_draw(r, 54, 430, s->toast, 1, 255, 255, 160);
    }
}

bool scene_click(Scene *s, int x, int y)
{
    if (s->video) {
        video_close(s->video);
        s->video = NULL;
        s->video_until = 0;
        release_media_buttons(s);
        restart_scene_music(s);
        return true;
    }
    if (s->info_overlay) {
        s->info_overlay = false;
        return true;
    }
    if (s->video_until) {
        s->video_until = 0;
        release_media_buttons(s);
        return true;
    }
    Comp *hit = scene_hit(s, x, y);
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "scene click %d,%d -> %s\n", x, y, hit ? hit->name : "-");
    if (modules_click(s, x, y, hit))
        return true;
    if (!hit)
        return false;

    /* Touch/no-hover aid: the letter playground objects are invisible until
       hovered. A direct first click reveals the object instead of leaving. */
    if (s->module == 2 && hit->state == ST_NORMAL && comp_frame_selector(hit) < 0) {
        comp_play_click(s, hit);
        hit->state = ST_ABNORMAL;
        play_state_wave(s, hit, ST_ABNORMAL);
        return true;
    }

    comp_play_click(s, hit);
    CompState old = hit->state;
    if (hit->state == ST_NORMAL && hit->click_to_abnormal)
        hit->state = ST_ABNORMAL;
    else if (hit->state == ST_ABNORMAL && hit->click_to_normal)
        hit->state = ST_NORMAL;
    if (hit->state != old)
        play_state_wave(s, hit, hit->state);
    if (hit->auto_click_exit && hit->modal_set)
        scene_request(s, hit->modal_result);
    return true;
}
