#define _GNU_SOURCE
#include "scene.h"
#include "font.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Polish titles transliterated to ASCII for the debug font. */
static const char *word_titles[136] = {
    [0] = "", [1] = "ADAPTER", [2] = "AGRAFKA", [3] = "AKWARIUM",
    [4] = "ANANAS", [5] = "ARBUZ", [6] = "AREK", [7] = "AUTO",
    [8] = "BALON", [9] = "BIEDRONKA", [10] = "BOISKO", [11] = "BRAMKA",
    [12] = "BUTY", [13] = "CEGLA", [14] = "CELINA", [15] = "CUKIEREK",
    [16] = "CYMBALKI", [17] = "CYRK", [18] = "CYTRYNA", [19] = "DAREK",
    [20] = "DOM", [21] = "DONICA", [22] = "DRABINA", [23] = "DROGA",
    [24] = "EDEK", [25] = "EKIERKA", [26] = "EKRAN", [27] = "EMU",
    [28] = "ESKIMOS", [29] = "FALA", [30] = "FARBY", [31] = "FOKA",
    [32] = "FORTEPIAN", [33] = "FOTEL", [34] = "GAJ", [35] = "GARNEK",
    [36] = "GITARA", [37] = "GNIAZDO", [38] = "GWIAZDY", [39] = "GWIZDEK",
    [40] = "HAMAK", [41] = "HARMONIA", [42] = "HELIKOPTER", [43] = "HERB",
    [44] = "HIPOPOTAM", [45] = "HULAJNOGA", [46] = "IGLO", [47] = "IGLA",
    [48] = "INDIANIN", [49] = "INDYK", [50] = "IREK", [51] = "JABLKO",
    [52] = "JAJKO", [53] = "JAMNIK", [54] = "JAREK", [55] = "JASKINIA",
    [56] = "JEZIORO", [57] = "KARUZELA", [58] = "KLOCKI", [59] = "KOMETA",
    [60] = "KONEWKA", [61] = "KOT", [62] = "KWIAT", [63] = "LAS",
    [64] = "LATARNIA", [65] = "LEW", [66] = "LODY", [67] = "LUCYNA",
    [68] = "LUNETA", [69] = "LAWKA", [70] = "LAZIENKA", [71] = "LUCJA",
    [72] = "LUK", [73] = "LZY", [74] = "MALPA", [75] = "MIASTO",
    [76] = "MLEKO", [77] = "MOST", [78] = "MOTOR", [79] = "MOTYL",
    [80] = "NAMIOT", [81] = "NARTY", [82] = "NEPTUN", [83] = "NICI",
    [84] = "NIEBO", [85] = "NOTES", [86] = "OAZA", [87] = "OBRUS",
    [88] = "OKULARY", [89] = "OLO", [90] = "OSA", [91] = "OWOCE",
    [92] = "PARASOL", [93] = "PIES", [94] = "PILKA", [95] = "PONTON",
    [96] = "PTAK", [97] = "PUDELKO", [98] = "RADIO", [99] = "RAK",
    [100] = "RAKIETA", [101] = "ROBOT", [102] = "ROWER", [103] = "RYBA",
    [104] = "SAD", [105] = "SAMOLOT", [106] = "SLONKO", [107] = "SMOK",
    [108] = "SOK", [109] = "SOWA", [110] = "STOLIK", [111] = "TELEFON",
    [112] = "TOLA", [113] = "", [114] = "TRAKTOR", [115] = "TRAWA",
    [116] = "TULIPAN", [117] = "TYGRYS", [118] = "UFOLUDEK", [119] = "UL",
    [120] = "ULA", [121] = "ULICA", [122] = "USTA", [123] = "WAGON",
    [124] = "WANNA", [125] = "WIATRAK", [126] = "WIELORYB", [127] = "WILK",
    [128] = "WULKAN", [129] = "ZAMEK", [130] = "ZEBRA", [131] = "ZEGAR",
    [132] = "ZENEK", [133] = "ZNAK", [134] = "ZYGZAK", [135] = "YETI",
};

const char *scene_word_title(int word)
{
    if (word < 0 || word > 135)
        return "";
    return word_titles[word];
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static bool name_has(Comp *c, const char *needle)
{
    return strcasestr(c->name, needle) != NULL;
}

static int comp_num(Comp *c, char prefix)
{
    if (!c->name[0] || c->name[0] != prefix)
        return -1;
    char *end = NULL;
    long v = strtol(c->name + 1, &end, 10);
    if (end == c->name + 1 || (end && *end))
        return -1;
    return (int)v;
}

static Comp *find_num(Scene *s, char prefix, int n)
{
    for (int i = 0; i < s->ncomps; i++) {
        if (comp_num(&s->comps[i], prefix) == n)
            return &s->comps[i];
    }
    return NULL;
}

static MIX_Audio *load_path(Scene *s, const char *win_path)
{
    if (!win_path || !*win_path)
        return NULL;
    char *p = resolve_asset(s->data_root, win_path);
    MIX_Audio *a = file_exists(p) ? audio_load_file(p) : NULL;
    free(p);
    return a;
}

static void click_visual(Scene *s, Comp *c)
{
    if (!c)
        return;
    comp_play_click(s, c);
    CompState old = c->state;
    if (c->state == ST_NORMAL && c->click_to_abnormal)
        c->state = ST_ABNORMAL;
    else if (c->state == ST_ABNORMAL && c->click_to_normal)
        c->state = ST_NORMAL;
    (void)old;
}

static void play_count(Scene *s)
{
    if (s->wav_count)
        audio_play(s->wav_count, 1.0f);
}

/* The task rules are the same cue HELP plays; kReset replays it after a
   reset instead of repeating kReset's own hover phrase. */
static void play_rules_cue(Scene *s)
{
    Comp *help = scene_find(s, "kHelp");
    if (help) {
        help->hover_wave_pending = false;
        if (help->wav_click)
            audio_play(help->wav_click, 1.0f);
    }
}

static void play_bad(Scene *s)
{
    /* The buzz is a short SFX: it may play over a spoken explanation. */
    if (s->wav_bad)
        audio_play_sfx(s->wav_bad, 0.7f);
    else if (s->wav_default)
        audio_play_sfx(s->wav_default, 0.6f);
}

static void make_paint_canvas(Scene *s)
{
    s->paint_px = xcalloc((size_t)ABC_W * ABC_H, sizeof(uint32_t));
    s->paint_bg = xcalloc((size_t)ABC_W * ABC_H, sizeof(uint32_t));
    for (int i = 0; i < ABC_W * ABC_H; i++) {
        s->paint_px[i] = 0xFF000000u;
        s->paint_bg[i] = 0xFF000000u;
    }
    Comp *bg = NULL;
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_IMAGE) {
            bg = &s->comps[i];
            break;
        }
    if (bg) {
        DfmValue *v = dfm_prop(bg->src, "Picture.Data");
        SDL_Surface *surf = scene_bmp_surface(v);
        if (surf) {
            SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
            SDL_DestroySurface(surf);
            if (rgba) {
                for (int y = 0; y < rgba->h; y++) {
                    int dy = y + s->origin_y;
                    if (dy < 0 || dy >= ABC_H)
                        continue;
                    uint32_t *src = (uint32_t *)((uint8_t *)rgba->pixels + y * rgba->pitch);
                    for (int x = 0; x < rgba->w; x++) {
                        int dx = x + s->origin_x;
                        if (dx < 0 || dx >= ABC_W)
                            continue;
                        s->paint_px[dy * ABC_W + dx] = src[x] | 0xFF000000u;
                    }
                }
                SDL_DestroySurface(rgba);
            }
        }
    }
    memcpy(s->paint_bg, s->paint_px, (size_t)ABC_W * ABC_H * sizeof(uint32_t));
    s->paint_tex = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STATIC, ABC_W, ABC_H);
    if (s->paint_tex) {
        SDL_SetTextureScaleMode(s->paint_tex, SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(s->paint_tex, NULL, s->paint_px, ABC_W * (int)sizeof(uint32_t));
    }
}

static void coloring_setup(Scene *s)
{
    s->palette_n = 0;
    for (int i = 1; i <= 14; i++) {
        Comp *k = find_num(s, 'k', i);
        if (!k) {
            s->palette[s->palette_n++] = 0xFFFFFFFFu;
            continue;
        }
        uint32_t col = scene_sample_bmp(k->src);
        if ((col & 0x00FFFFFFu) == 0)
            col = 0xFFFFFFFFu;
        if (getenv("ABC_DEBUG"))
            fprintf(stderr, "palette %d: %08x\n", i, col);
        s->palette[s->palette_n++] = col;
    }
    s->sel_color = 0;
    make_paint_canvas(s);
    /* Hide the original background; the paint canvas replaces it. */
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_IMAGE)
            s->comps[i].visible = false;
}

static bool in_rect(Scene *s, Comp *c, int x, int y)
{
    return comp_contains(s, c, x, y);
}

static void flood_fill(Scene *s, int sx, int sy, SDL_Rect *area)
{
    if (!s->paint_px || s->palette_n <= 0)
        return;
    if (sx < area->x || sy < area->y || sx >= area->x + area->w || sy >= area->y + area->h)
        return;
    uint32_t target = s->paint_px[sy * ABC_W + sx];
    uint32_t fill = s->palette[s->sel_color] | 0xFF000000u;
    if ((target & 0x00FFFFFFu) == (fill & 0x00FFFFFFu))
        return;
    int tr = (target >> 16) & 255, tg = (target >> 8) & 255, tb = target & 255;
    if (tr + tg + tb < 100)
        return;
    int cap = ABC_W * ABC_H;
    int *stack = xmalloc((size_t)cap * sizeof(int));
    int sp = 0;
    stack[sp++] = sy * ABC_W + sx;
    int filled = 0;
    while (sp > 0 && filled < cap) {
        int p = stack[--sp];
        uint32_t c = s->paint_px[p];
        if ((c & 0x00FFFFFFu) == (fill & 0x00FFFFFFu))
            continue;
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        if (r + g + b < 100)
            continue;
        if (abs(r - tr) + abs(g - tg) + abs(b - tb) > 150)
            continue;
        s->paint_px[p] = fill;
        filled++;
        int x = p % ABC_W, y = p / ABC_W;
        if (x > area->x)
            stack[sp++] = p - 1;
        if (x + 1 < area->x + area->w)
            stack[sp++] = p + 1;
        if (y > area->y)
            stack[sp++] = p - ABC_W;
        if (y + 1 < area->y + area->h)
            stack[sp++] = p + ABC_W;
        if (sp > cap - 8)
            break;
    }
    free(stack);
    s->paint_dirty = true;
}

static void maze_pick_start(Comp *maze, int *sx, int *sy);
static const char *media_file(Scene *s, const char *comp_name);

static void reset_game(Scene *s)
{
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        c->state = ST_NORMAL;
        c->found = c->placed = c->selected = false;
        c->placed_by = 0;
        c->hover_lock = false;
        c->hover_wave_pending = false;
        c->anim_playing = false;
        c->visible = c->visible0;
        if (c->cls == CC_ZAMIEN || c->cls == CC_WYSUN) {
            c->left = c->home_left;
            c->top = c->home_top;
        }
        /* Objects whose bitmap was forced visible by gameplay. */
        if (c->cls == CC_MAPOWANIE && c->src) {
            c->normal = (int)dfm_int(c->src, "_BMPNumerNormal", -2);
            c->abnormal = (int)dfm_int(c->src, "_BMPNumerAbnormal", -2);
            c->special = (int)dfm_int(c->src, "_BMPNumerSpecial", -2);
            c->autosize = dfm_bool(c->src, "_AutoSize", false);
        }
        if (c->is_anim) {
            c->visible = c->visible0;
            c->anim_index = 0;
        }
    }
    s->found_count = 0;
    s->complete = false;
    s->selected_idx = -1;
    s->selected_group = 0;
    s->drag_idx = -1;
    if (s->paint_bg && s->paint_px) {
        memcpy(s->paint_px, s->paint_bg, (size_t)ABC_W * ABC_H * sizeof(uint32_t));
        s->paint_dirty = true;
    }
    if (s->module == 14 && s->player_init) {
        for (int i = 0; i < s->ncomps; i++) {
            Comp *c = &s->comps[i];
            if (c->cls == CC_LABIRYNT) {
                scene_set_player(s, s->origin_x + c->left + c->entry_x,
                                 s->origin_y + c->top + c->entry_y);
                break;
            }
        }
    }
}

void modules_setup(Scene *s, const char *data_root)
{
    (void)data_root;

    /* Background music playlist. */
    Comp *midi = NULL;
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_MIDI) {
            midi = &s->comps[i];
            break;
        }
    if (midi) {
        s->nmusic = 0;
        for (int i = 1; i <= 4; i++) {
            char key[16];
            snprintf(key, sizeof(key), "_File%d", i);
            const char *f = dfm_str(midi->src, key, NULL);
            if (!f || !*f)
                continue;
            char *p = resolve_asset(s->data_root, f);
            snprintf(s->music_paths[s->nmusic], sizeof(s->music_paths[0]), "%s", p);
            free(p);
            s->nmusic++;
        }
        if (s->nmusic > 0) {
            const char *base = strrchr(s->music_paths[0], '/');
            snprintf(s->music_sig, sizeof(s->music_sig), "midi:%s", base ? base + 1 : s->music_paths[0]);
            const char *paths[4];
            for (int i = 0; i < s->nmusic; i++)
                paths[i] = s->music_paths[i];
            audio_music_play(paths, s->nmusic, true, s->music_sig);
        }
    }

    /* Scene-level sound players, matched by their instance names. */
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (c->cls == CC_WAVE) {
            if (name_has(c, "zle") || name_has(c, "bad") || name_has(c, "alarm")) {
                if (!s->wav_bad) s->wav_bad = c->wav_normal;
            } else if (name_has(c, "ok") || (c->name[0] && c->name[strlen(c->name) - 1] == 'K')) {
                if (!s->wav_ok) s->wav_ok = c->wav_normal;
            } else if (name_has(c, "wybor") || name_has(c, "select")) {
                if (!s->wav_select) s->wav_select = c->wav_normal;
            } else if (!s->wav_default) {
                s->wav_default = c->wav_normal;
            }
        }
    }

    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_WYSUN)
            s->has_toolbar = true;

    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (c->cls == CC_CHECK) {
            s->target_count = (int)dfm_int(c->src, "_Licznik", 0);
            s->wav_count = load_path(s, dfm_str(c->src, "_WaveLicznik", NULL));
            break;
        }
    }

    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (c->cls == CC_MAPOW) {
            c->nmaphs = 0;
            DfmValue *v = dfm_prop(c->src, "_MapHS.Strings");
            if (v && v->type == DFM_LIST) {
                for (int j = 0; j < v->count && c->nmaphs < 8; j++) {
                    if (v->items[j].type == DFM_STR) {
                        snprintf(c->maphs[c->nmaphs], sizeof(c->maphs[0]), "%s", v->items[j].s);
                        c->nmaphs++;
                    }
                }
            }
        }
    }

    /* Telewizorek: mark media buttons whose clip is absent (the original
       draws these yellow). */
    if (s->module == 3) {
        static const char *buttons[3] = {"KlTabl", "KlKamera", "KlAparat"};
        static const char *media[3] = {"aviLit", "aviFilm", "aviFoto"};
        int word = s->id % 1000;
        for (int i = 0; i < 3; i++) {
            Comp *b = scene_find(s, buttons[i]);
            const char *f = media_file(s, media[i]);
            if (!b) {
                continue;
            }
            /* Film/photo must be the clip belonging to this word.  The disc
               data reuses 9_2.AVI for words 6 and 135; treat a missing
               <word>_1/<word>_2 clip as an absent button instead of playing
               an unrelated clip. */
            char expected[64];
            const char *check = f;
            if (i == 1 || i == 2) {
                snprintf(expected, sizeof(expected), "\\AVI\\%d_%d.AVI",
                         word, i);
                check = expected;
            }
            bool exists = false;
            if (f && check) {
                char *p = resolve_asset(s->data_root, check);
                exists = file_exists(p);
                if (getenv("ABC_DEBUG"))
                    fprintf(stderr, "media %s <- %s (%s) disabled=%d\n",
                            buttons[i], check, p, (int)!exists);
                free(p);
            }
            b->disabled = !exists;
        }
    }

    /* kBack gives the return scene; fall back to the owning word. */
    Comp *back = scene_find(s, "kBack");
    if (back && back->modal_set)
        s->return_id = back->modal_result;

    if (s->module == 10)
        coloring_setup(s);

    if (s->module == 15 && s->target_count <= 0)
        s->target_count = 1;

    if (s->module == 11) {
        if (s->target_count <= 0) {
            int n = 0;
            for (int i = 0; i < s->ncomps; i++) {
                Comp *c = &s->comps[i];
                int pi = comp_num(c, 'k');
                if (pi > 0 && c->tex && c->visible)
                    n++;
            }
            s->target_count = n;
        }
    }

    if (s->module == 12) {
        /* Completion means every target region is filled.  Several parts
           can be alternatives for one target (AUTO has six targets and ten
           usable parts), so count distinct target names, not parts. */
        char names[16][24];
        int n = 0;
        for (int i = 0; i < s->ncomps; i++) {
            Comp *c = &s->comps[i];
            if (c->cls != CC_MAPOW)
                continue;
            for (int k = 0; k < c->nmaphs; k++) {
                bool seen = false;
                for (int j = 0; j < n; j++)
                    if (str_ieq(names[j], c->maphs[k]))
                        seen = true;
                if (!seen && n < 16)
                    snprintf(names[n++], sizeof(names[0]), "%s", c->maphs[k]);
            }
        }
        s->target_count = n;
    }

    if (s->module == 17 && s->target_count <= 0) {
        int n = 0;
        for (int i = 0; i < s->ncomps; i++) {
            Comp *c = &s->comps[i];
            if (comp_num(c, 'o') > 0 && c->visible)
                n++;
        }
        s->target_count = n;
    }

    if (s->module == 14) {
        for (int i = 0; i < s->ncomps; i++) {
            Comp *c = &s->comps[i];
            if (c->cls != CC_LABIRYNT)
                continue;
            int sx, sy;
            maze_pick_start(c, &sx, &sy);
            c->entry_x = sx;
            c->entry_y = sy;
            scene_set_player(s, s->origin_x + c->left + sx,
                             s->origin_y + c->top + sy);
            if (getenv("ABC_DEBUG") && c->collision) {
                int lx = c->entry_x, ly = c->entry_y;
                fprintf(stderr, "maze %dx%d start cell=%d,%d player=%d,%d collision=%d\n",
                        c->col_w, c->col_h, lx, ly, s->player_x, s->player_y,
                        c->collision[ly * c->col_w + lx]);
            }
            int word = s->id % 1000;
            char path[64];
            snprintf(path, sizeof(path), "\\WAV\\L%dD.WAV", word);
            s->maze_ok = load_path(s, path);
            snprintf(path, sizeof(path), "\\WAV\\L%dZ1.WAV", word);
            s->maze_bad1 = load_path(s, path);
            snprintf(path, sizeof(path), "\\WAV\\L%dZ2.WAV", word);
            s->maze_bad2 = load_path(s, path);
            if (s->maze_ok)
                s->wav_ok = s->maze_ok;
            break;
        }
    }

    /* Zgadywanka 2/4: count the actual targets when the checker is a sentinel. */
    if (s->module == 16 || s->module == 18) {
        int visible_o = 0;
        for (int i = 0; i < s->ncomps; i++) {
            Comp *c = &s->comps[i];
            if (c->name[0] == 'o' && comp_num(c, 'o') > 0 && c->visible)
                visible_o++;
        }
        if (s->target_count == 0 && visible_o > 0)
            s->target_count = visible_o;
        if (s->target_count >= 100)
            s->no_completion = true;
    }
}

/* ---------------- colouring ---------------- */

static bool coloring_click(Scene *s, int x, int y, Comp *hit)
{
    if (hit && comp_num(hit, 'k') > 0 && comp_num(hit, 'k') <= s->palette_n) {
        int idx = comp_num(hit, 'k') - 1;
        if (getenv("ABC_DEBUG"))
            fprintf(stderr, "palette click %s -> %d colour %08x\n", hit->name, idx,
                    s->palette[idx]);
        s->sel_color = idx;
        for (int i = 0; i < s->palette_n; i++) {
            Comp *k = find_num(s, 'k', i + 1);
            if (!k)
                continue;
            k->state = (i == idx) ? ST_ABNORMAL : ST_NORMAL;
        }
        click_visual(s, hit);
        return true;
    }
    Comp *hs = scene_find(s, "HSMalowanka");
    if (hs) {
        SDL_FRect r;
        comp_rect(s, hs, &r);
        SDL_Rect area = {(int)r.x, (int)r.y, (int)r.w, (int)r.h};
        if (x >= area.x && y >= area.y && x < area.x + area.w && y < area.y + area.h) {
            flood_fill(s, x, y, &area);
            return true;
        }
    }
    return false;
}

/* ---------------- puzzle ---------------- */

static bool puzzle_place(Scene *s, int piece_idx, Comp *slot)
{
    Comp *piece = find_num(s, 'k', piece_idx);
    if (!piece || !slot || piece->placed)
        return false;
    int slot_idx = comp_num(slot, 'p');
    if (slot_idx != piece_idx)
        return false;
    /* The placed frame has a transparent border; align the visible picture
       content with the slot, not the frame's outer corner. */
    piece->left = slot->left - piece->content_off_x;
    piece->top = slot->top - piece->content_off_y;
    piece->slot_w = slot->width;
    piece->slot_h = slot->height;
    piece->state = ST_ABNORMAL;
    piece->placed = true;
    piece->hover_lock = true;
    s->found_count++;
    /* Short placement click per piece; the long congratulation cue is only
       played when the whole puzzle is complete (see scene_complete). */
    if (s->wav_select)
        audio_play(s->wav_select, 0.8f);
    if (s->found_count >= s->target_count)
        scene_complete(s, 1800);
    return true;
}

static void revert_piece_to_tray(Scene *s, Comp *piece)
{
    (void)s;
    if (!piece)
        return;
    /* Wrong assignment does not disable/gray the piece: it simply returns
       to its previous (tray) visual state and remains retryable. */
    piece->state = ST_NORMAL;
    piece->hover_wave_pending = false;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "piece %s wrong -> tray state\n", piece->name);
}

static bool puzzle_click(Scene *s, int x, int y, Comp *hit)
{
    if (hit && comp_num(hit, 'k') > 0 && hit->tex && !hit->placed) {
        s->selected_idx = comp_num(hit, 'k');
        for (int i = 1; i <= 14; i++) {
            Comp *k = find_num(s, 'k', i);
            if (k)
                k->selected = (comp_num(k, 'k') == s->selected_idx);
        }
        /* Selecting a piece must not switch it to the "placed" frame. */
        hit->state = ST_NORMAL;
        comp_play_click(s, hit);
        return true;
    }
    /* Any slot under the cursor. */
    for (int i = 1; i <= 14; i++) {
        Comp *p = find_num(s, 'p', i);
        if (!p || !p->visible)
            continue;
        if (in_rect(s, p, x, y)) {
            if (s->selected_idx > 0) {
                if (puzzle_place(s, s->selected_idx, p)) {
                    s->selected_idx = -1;
                    return true;
                }
                revert_piece_to_tray(s, find_num(s, 'k', s->selected_idx));
                play_bad(s);
                s->selected_idx = -1;
            }
            return true;
        }
    }
    s->selected_idx = -1;
    return false;
}

/* ---------------- skladanka ---------------- */

static SDL_Rect clamped_rect(Scene *s, Comp *c)
{
    SDL_FRect r;
    comp_rect(s, c, &r);
    SDL_Rect out = {(int)r.x, (int)r.y, (int)r.w, (int)r.h};
    if (out.x < 0) { out.w += out.x; out.x = 0; }
    if (out.y < 0) { out.h += out.y; out.y = 0; }
    if (out.x + out.w > ABC_W) out.w = ABC_W - out.x;
    if (out.y + out.h > ABC_H) out.h = ABC_H - out.y;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static bool skladanka_click(Scene *s, int x, int y, Comp *hit)
{
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "skladanka click %d,%d hit=%s\n", x, y,
                hit ? hit->name : "-");

    /* The original is a one-click assembly: clicking a tray part puts it in
       its designated place on the picture immediately.  There is no second
       drop click (verified against ABC24.EXE for AUTO/12007). */
    Comp *piece = NULL;
    for (int i = 1; i <= 20; i++) {
        Comp *p = find_num(s, 's', i);
        if (!p || !p->visible || p->nmaphs == 0)
            continue;
        SDL_Rect r = clamped_rect(s, p);
        /* Later components are on top; in KWIAT s5 is completely covered
           by the earlier s3 rectangle, so the last match must win. */
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
            piece = p;
    }
    if (!piece)
        return hit != NULL;

    /* A part may list more than one compatible target (SAMOLOT has two
       interchangeable parts for slots m2/m4).  Prefer a still-empty slot,
       otherwise replace the first one, as AUTO's alternative car bodies do. */
    Comp *first = NULL, *target = NULL;
    for (int k = 0; k < piece->nmaphs && !target; k++) {
        Comp *m = scene_find(s, piece->maphs[k]);
        if (!m)
            continue;
        if (!first)
            first = m;
        if (!m->placed)
            target = m;
    }
    if (!target)
        target = first;
    if (!target)
        return true;

    int pnum = comp_num(piece, 's');

    /* Clicking the same part again takes it back out.  Clicking another
       alternative for the same slot replaces the current design instead. */
    if (target->placed && target->placed_by == pnum) {
        target->visible = false;
        target->placed = false;
        target->placed_by = 0;
        if (s->found_count > 0)
            s->found_count--;
        if (s->complete && s->found_count < s->target_count) {
            s->complete = false;
            s->complete_at = 0;
        }
        if (getenv("ABC_DEBUG"))
            fprintf(stderr, "  remove s%d <- %s found=%d/%d\n", pnum,
                    target->name, s->found_count, s->target_count);
        return true;
    }

    /* TMapowanie coordinates are relative to the TTloMapowanie picture. */
    Comp *map = scene_find(s, "map");
    int ox = map ? (int)lroundf(map->left) : 0;
    int oy = map ? (int)lroundf(map->top) : 0;

    bool newly_filled = !target->placed;
    target->visible = true;
    target->state = ST_NORMAL;
    /* The piece's special frame selects the design of the target part. */
    target->normal = (piece->special > 0) ? piece->special : 1;
    target->autosize = true;
    target->left = target->home_left + ox;
    target->top = target->home_top + oy;
    target->hover_lock = true;
    target->placed = true;
    target->placed_by = pnum;
    if (newly_filled)
        s->found_count++;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "  place s%d -> %s frame %d at %d,%d newly=%d found=%d/%d\n",
                pnum, target->name, target->normal,
                (int)target->left, (int)target->top, (int)newly_filled,
                s->found_count, s->target_count);
    play_count(s);
    if (s->found_count >= s->target_count)
        scene_complete(s, 1800);
    return true;
}

/* ---------------- spot the difference ---------------- */

static bool szczegoly_click(Scene *s, int x, int y, Comp *hit)
{
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (hit) {
        int xi = comp_num(hit, 'x');
        int yi = comp_num(hit, 'y');
        if ((xi > 0 || yi > 0) && !hit->found) {
            int n = xi > 0 ? xi : yi;
            Comp *a = find_num(s, 'x', n);
            Comp *b = find_num(s, 'y', n);
            if (a) { a->found = true; a->hover_lock = true; }
            if (b) { b->found = true; b->hover_lock = true; }
            s->found_count++;
            play_count(s);
            if (s->found_count >= s->target_count)
                scene_complete(s, 1800);
            return true;
        }
    }
    play_bad(s);
    return hit != NULL;
}

/* ---------------- zgadywanka 1 ---------------- */

static bool zg1_click(Scene *s, int x, int y, Comp *hit)
{
    (void)x;
    (void)y;
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (hit) {
        int oi = comp_num(hit, 'o');
        int zi = comp_num(hit, 'z');
        if (oi > 0) {
            /* o1 carries the correct-choice cue (WAV/1d.WAV).  waveK in this
               scene is an empty player, so play the hotspot's own sound. */
            comp_play_click(s, hit);
            Comp *anim = scene_find(s, "AnimOK");
            if (anim)
                comp_anim_start(s, anim, true);
            s->complete_delay_ms = 8400;
            scene_complete(s, 8400);
            return true;
        }
        if (zi > 0) {
            /* Speak the wrongly chosen item's explanation, with the short
               wrong-answer buzz mixed on top (buzz is SFX, not speech). */
            comp_play_click(s, hit);
            Comp *anim = scene_find(s, "AnimZle");
            if (anim)
                comp_anim_start(s, anim, true);
            play_bad(s);
            return true;
        }
    }
    play_bad(s);
    return hit != NULL;
}

/* ---------------- zgadywanka 2 ---------------- */

static bool zg2_click(Scene *s, int x, int y, Comp *hit)
{
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (!hit) {
        play_bad(s);
        return false;
    }
    int oi = comp_num(hit, 'o');
    int ni = comp_num(hit, 'n');
    if (oi > 0 && !hit->found) {
        hit->found = true;
        char zname[16];
        snprintf(zname, sizeof(zname), "z%d", oi);
        Comp *z = scene_find(s, zname);
        if (z) {
            z->state = ST_ABNORMAL;
            z->hover_lock = true;
        }
        s->found_count++;
        play_count(s);
        if (s->target_count > 0 && s->found_count >= s->target_count) {
            Comp *anim = scene_find(s, "Anim");
            if (anim)
                comp_anim_start(s, anim, true);
            scene_complete(s, 6500);
        }
        return true;
    }
    if (ni > 0) {
        play_bad(s);
        return true;
    }
    return false;
}

/* ---------------- zgadywanka 3 ---------------- */

static bool zg3_click(Scene *s, int x, int y, Comp *hit)
{
    (void)x;
    (void)y;
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (!hit) {
        play_bad(s);
        return false;
    }
    int oi = comp_num(hit, 'o');
    int zi = comp_num(hit, 'z');
    int n = oi > 0 ? oi : zi;
    if (n <= 0)
        return false;
    int group = oi > 0 ? 1 : 2;
    if (s->selected_idx < 0) {
        s->selected_idx = n;
        s->selected_group = group;
        hit->selected = true;
        if (oi > 0)
            comp_set_state(s, hit, ST_SPECIAL);
        click_visual(s, hit);
        return true;
    }
    int sel = s->selected_idx;
    if (sel == n && s->selected_group == group) {
        /* second click on the same item deselects it */
        s->selected_idx = -1;
        Comp *o = find_num(s, 'o', sel);
        if (o)
            o->state = ST_NORMAL;
        return true;
    }
    if (sel == n && s->selected_group != group) {
        /* Correct pair. */
        Comp *o = find_num(s, 'o', n);
        Comp *z = find_num(s, 'z', n);
        if (o) {
            o->state = ST_SPECIAL;
            o->hover_lock = true;
            o->found = true;
        }
        if (z) {
            z->hover_lock = true;
            z->found = true;
        }
        s->found_count++;
        play_count(s);
        s->selected_idx = -1;
        s->selected_group = 0;
        if (s->found_count >= s->target_count)
            scene_complete(s, 1800);
        return true;
    }
    Comp *o = find_num(s, 'o', sel);
    if (o)
        o->state = ST_NORMAL;
    s->selected_idx = -1;
    play_bad(s);
    return true;
}

/* ---------------- zgadywanka 4 ---------------- */

static bool zg4_click(Scene *s, int x, int y, Comp *hit)
{
    (void)x;
    (void)y;
    if (hit && hit->cls == CC_WYSUN)
        return false;
    if (hit) {
        int oi = comp_num(hit, 'o');
        if (oi > 0 && !hit->found) {
            hit->found = true;
            hit->state = ST_ABNORMAL;
            hit->hover_lock = true;
            s->found_count++;
            play_count(s);
            if (!s->no_completion && s->target_count > 0 &&
                s->found_count >= s->target_count)
                scene_complete(s, 1800);
            return true;
        }
    }
    play_bad(s);
    return hit != NULL;
}

/* ---------------- maze ---------------- */

static bool maze_free(Scene *s, Comp *maze, int x, int y)
{
    if (!maze->collision)
        return true;
    int lx = x - (s->origin_x + maze->left);
    int ly = y - (s->origin_y + maze->top);
    /* Outside the collision map is open space; the player is clamped later. */
    if (lx < 0 || ly < 0 || lx >= maze->col_w || ly >= maze->col_h)
        return true;
    return maze->collision[ly * maze->col_w + lx] == 0;
}

static bool maze_cell_free(Comp *maze, int x, int y)
{
    if (!maze->collision)
        return true;
    if (x < 0 || y < 0 || x >= maze->col_w || y >= maze->col_h)
        return false;
    return maze->collision[y * maze->col_w + x] == 0;
}

static bool maze_cell_clear(Comp *maze, int x, int y)
{
    return maze_cell_free(maze, x, y) && maze_cell_free(maze, x + 2, y) &&
           maze_cell_free(maze, x - 2, y) && maze_cell_free(maze, x, y + 2) &&
           maze_cell_free(maze, x, y - 2);
}

/* Find a free starting cell near the scene's declared entrance. */
static void maze_pick_start(Comp *maze, int *sx, int *sy)
{
    if (!maze->collision) {
        *sx = maze->entry_x;
        *sy = maze->entry_y;
        return;
    }
    int ex = maze->entry_x, ey = maze->entry_y;
    if (maze_cell_free(maze, ex, ey)) {
        *sx = ex;
        *sy = ey;
        return;
    }
    for (int r = 0; r < 80; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) + abs(dy) != r)
                    continue;
                int x = ex + dx, y = ey + dy;
                if (maze_cell_clear(maze, x, y)) {
                    *sx = x;
                    *sy = y;
                    return;
                }
            }
        }
    }
    *sx = ex;
    *sy = ey;
}

static bool maze_clear(Scene *s, Comp *maze, int x, int y)
{
    return maze_free(s, maze, x, y) && maze_free(s, maze, x + 2, y) &&
           maze_free(s, maze, x - 2, y) && maze_free(s, maze, x, y + 2) &&
           maze_free(s, maze, x, y - 2);
}

static void maze_wrong(Scene *s, Comp *maze, bool second)
{
    if (second ? s->maze_bad2 : s->maze_bad1)
        audio_play(second ? s->maze_bad2 : s->maze_bad1, 1.0f);
    else
        play_bad(s);
    scene_set_player(s, s->origin_x + maze->left + maze->entry_x,
                     s->origin_y + maze->top + maze->entry_y);
    scene_toast(s, "SPROBUJ INNA DROGE", 1600);
}

static void maze_update(Scene *s, float dt)
{
    Comp *maze = NULL;
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_LABIRYNT) {
            maze = &s->comps[i];
            break;
        }
    if (!maze || !s->player_init || s->complete)
        return;
    int tx = s->mouse_x, ty = s->mouse_y;
    int mx0 = s->origin_x + maze->left, my0 = s->origin_y + maze->top;
    bool pointer_in_maze = tx >= mx0 && ty >= my0 &&
                           tx < mx0 + maze->col_w && ty < my0 + maze->col_h;
    if (!pointer_in_maze) {
        /* Still allow reaching an exit right at the boundary. */
        Comp *good = scene_find(s, "WyjDobre");
        if (good && in_rect(s, good, s->player_x, s->player_y))
            scene_complete(s, 1800);
        return;
    }
    int dx = tx - s->player_x, dy = ty - s->player_y;
    float len = sqrtf((float)(dx * dx + dy * dy));
    if (len < 3.0f)
        return;
    float speed = (float)maze->max_speed * 12.0f * dt;
    if (speed < 1.0f) speed = 1.0f;
    if (speed > len) speed = len;
    int steps = (int)speed;
    if (steps < 1)
        steps = 1;
    bool moved = false;
    for (int i = 0; i < steps; i++) {
        int sxn = (tx > s->player_x) - (tx < s->player_x);
        int syn = (ty > s->player_y) - (ty < s->player_y);
        if (!sxn && !syn)
            break;
        if (maze_clear(s, maze, s->player_x + sxn, s->player_y + syn)) {
            s->player_x += sxn;
            s->player_y += syn;
            moved = true;
        } else if (sxn && maze_clear(s, maze, s->player_x + sxn, s->player_y)) {
            s->player_x += sxn;
            moved = true;
        } else if (syn && maze_clear(s, maze, s->player_x, s->player_y + syn)) {
            s->player_y += syn;
            moved = true;
        } else {
            break;
        }
    }
    if (getenv("ABC_DEBUG")) {
        static int dbg;
        if (++dbg % 60 == 0)
            fprintf(stderr, "maze player %d,%d target %d,%d moved=%d\n",
                    s->player_x, s->player_y, tx, ty, (int)moved);
    }
    if (s->player_x < mx0 + 4) s->player_x = mx0 + 4;
    if (s->player_x > mx0 + maze->col_w - 5) s->player_x = mx0 + maze->col_w - 5;
    if (s->player_y < my0 + 4) s->player_y = my0 + 4;
    if (s->player_y > my0 + maze->col_h - 5) s->player_y = my0 + maze->col_h - 5;

    Comp *good = scene_find(s, "WyjDobre");
    Comp *bad1 = scene_find(s, "WyjZle1");
    Comp *bad2 = scene_find(s, "WyjZle2");
    if (good && in_rect(s, good, s->player_x, s->player_y)) {
        scene_complete(s, 1800);
        return;
    }
    if (bad1 && in_rect(s, bad1, s->player_x, s->player_y)) {
        maze_wrong(s, maze, false);
        return;
    }
    if (bad2 && in_rect(s, bad2, s->player_x, s->player_y)) {
        maze_wrong(s, maze, true);
    }
}

/* ---------------- train ---------------- */

static void train_update(Scene *s, float dt)
{
    Comp *train = NULL;
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_PRZESUN) {
            train = &s->comps[i];
            break;
        }
    if (!train)
        return;
    int max_scroll = train->tex_w - train->width;
    if (max_scroll < 0)
        max_scroll = 0;
    bool in_strip = s->mouse_y >= s->origin_y + train->top &&
                    s->mouse_y < s->origin_y + train->top + train->height;
    float speed = 320.0f;
    if (in_strip && s->mouse_x < 200)
        s->scroll -= (int)(speed * dt);
    else if (in_strip && s->mouse_x > ABC_W - 200)
        s->scroll += (int)(speed * dt);
    if (s->scroll < 0) s->scroll = 0;
    if (s->scroll > max_scroll) s->scroll = max_scroll;
}

static bool train_click(Scene *s, int x, int y, Comp *hit)
{
    Comp *train = NULL;
    for (int i = 0; i < s->ncomps; i++)
        if (s->comps[i].cls == CC_PRZESUN) {
            train = &s->comps[i];
            break;
        }
    if (train && y >= s->origin_y + train->top &&
        y < s->origin_y + train->top + train->height) {
        for (int i = 0; i < train->box_count; i++) {
            int bx = s->origin_x + train->left + train->boxes[i].x - s->scroll;
            int bw = train->boxes[i].w;
            if (x >= bx && x < bx + bw) {
                if (getenv("ABC_DEBUG"))
                    fprintf(stderr, "train box %d target %d (scroll %d)\n",
                            i, train->box_targets[i], s->scroll);
                click_visual(s, train);
                scene_request(s, train->box_targets[i]);
                return true;
            }
        }
        if (getenv("ABC_DEBUG")) {
            int near = -1, nd = 99999;
            for (int i = 0; i < train->box_count; i++) {
                int bx = s->origin_x + train->left + train->boxes[i].x - s->scroll;
                int d = abs(x - (bx + train->boxes[i].w / 2));
                if (d < nd) { nd = d; near = i; }
            }
            fprintf(stderr, "train miss x=%d scroll=%d boxes=%d nearest=%d at %d\n",
                    x, s->scroll, train->box_count, near,
                    near >= 0 ? s->origin_x + train->left + train->boxes[near].x - s->scroll : -1);
        }
    }
    if (hit && str_ieq(hit->name, "info")) {
        click_visual(s, hit);
        char *path = resolve_asset(s->data_root, "\\AVI\\AUTORZY.AVI");
        if (file_exists(path)) {
            s->video = video_open(s->renderer, path);
            s->video_fullscreen = true;
            if (s->video) {
                /* The credits clip has its own soundtrack; the train music
                   resumes when the clip ends or is dismissed. */
                audio_music_pause();
                s->video_stop_music = true;
            }
        }
        free(path);
        if (!s->video)
            s->info_overlay = true;
        return true;
    }
    return false;
}

/* ---------------- TV ---------------- */

static const char *media_file(Scene *s, const char *comp_name)
{
    Comp *c = scene_find(s, comp_name);
    if (!c || c->cls != CC_MEDIA)
        return NULL;
    return dfm_str(c->src, "FileName", NULL);
}

static bool tv_click(Scene *s, int x, int y, Comp *hit)
{
    (void)x;
    (void)y;
    if (!hit)
        return false;
    const char *which = NULL;
    const char *label = NULL;
    char expected[64];
    int word = s->id % 1000;
    if (str_ieq(hit->name, "KlTabl")) {
        which = media_file(s, "aviLit");
        label = "LITERA";
    } else if (str_ieq(hit->name, "KlKamera")) {
        snprintf(expected, sizeof(expected), "\\AVI\\%d_1.AVI", word);
        which = expected;
        label = "FILM";
    } else if (str_ieq(hit->name, "KlAparat")) {
        snprintf(expected, sizeof(expected), "\\AVI\\%d_2.AVI", word);
        which = expected;
        label = "FOTO";
    }
    if (!which)
        return false; /* not a media button: let the generic handler run */
    /* Missing or foreign clip: ignore the click (the button is drawn yellow). */
    if (hit->disabled) {
        if (getenv("ABC_DEBUG"))
            fprintf(stderr, "disabled TV button %s ignored\n", hit->name);
        return true;
    }
    char *path = resolve_asset(s->data_root, which);
    bool exists = file_exists(path);
    click_visual(s, hit);
    if (!exists) {
        free(path);
        return true;
    }
    Comp *ekran = scene_find(s, "Ekran");
    if (ekran) {
        SDL_FRect r;
        comp_rect(s, ekran, &r);
        s->video_rect = (SDL_Rect){(int)r.x, (int)r.y, (int)r.w, (int)r.h};
    } else {
        s->video_rect = (SDL_Rect){180, 90, 380, 290};
    }
    if (s->video_rect.w < 40)
        s->video_rect = (SDL_Rect){180, 90, 380, 290};

    if (s->video) {
        video_close(s->video);
        s->video = NULL;
    }
    bool music_active = audio_music_signature() != NULL;
    s->video = video_open(s->renderer, path);
    s->video_fullscreen = false;
    if (!s->video) {
        /* FFmpeg unavailable or codec unsupported: keep a placeholder card. */
        const char *base = strrchr(which, '\\');
        if (!base) base = strrchr(which, '/');
        snprintf(s->video_label, sizeof(s->video_label), "%s: %s", label,
                 base ? base + 1 : which);
        s->video_until = SDL_GetTicks() + 3000;
    } else if (music_active) {
        /* TV clips carry their own soundtrack; pause the scene music until
           playback ends or the clip is dismissed. */
        audio_music_pause();
        s->video_stop_music = true;
        if (getenv("ABC_DEBUG"))
            log_info("TV video: background music paused");
    }
    free(path);
    return true;
}

/* ---------------- module dispatch ---------------- */

void modules_update(Scene *s, float dt)
{
    switch (s->module) {
    case 0: {
        uint64_t ms = 4000;
        for (int i = 0; i < s->ncomps; i++)
            if (s->comps[i].cls == CC_TIMER)
                ms = (uint64_t)dfm_int(s->comps[i].src, "Interval", 4000);
        if (s->now - s->start_time >= ms)
            scene_request(s, 1001);
        break;
    }
    case 1:
        train_update(s, dt);
        break;
    case 14:
        maze_update(s, dt);
        break;
    case 11:
        if (s->drag_idx >= 0 && s->mouse_down) {
            Comp *piece = &s->comps[s->drag_idx];
            piece->left = s->mouse_x - s->drag_dx - s->origin_x;
            piece->top = s->mouse_y - s->drag_dy - s->origin_y;
        }
        break;
    default:
        break;
    }
}

void modules_mouse_down(Scene *s, int x, int y)
{
    if (s->module == 11) {
        for (int i = s->ncomps - 1; i >= 0; i--) {
            Comp *c = &s->comps[i];
            if (comp_num(c, 'k') <= 0 || !c->tex || c->placed || !c->visible)
                continue;
            if (in_rect(s, c, x, y)) {
                SDL_FRect r;
                comp_rect(s, c, &r);
                s->drag_idx = i;
                s->selected_idx = comp_num(c, 'k');
                s->drag_dx = x - (int)r.x;
                s->drag_dy = y - (int)r.y;
                break;
            }
        }
    }
}

void modules_mouse_up(Scene *s, int x, int y)
{
    if (s->module != 11 || s->drag_idx < 0)
        return;
    Comp *piece = &s->comps[s->drag_idx];
    int pi = comp_num(piece, 'k');
    /* A plain click (no drag) keeps the piece selected for a later slot click. */
    if (abs(piece->left - piece->home_left) <= 6 && abs(piece->top - piece->home_top) <= 6) {
        piece->left = piece->home_left;
        piece->top = piece->home_top;
        s->drag_idx = -1;
        return;
    }
    bool placed = false;
    for (int i = 1; i <= 14; i++) {
        Comp *p = find_num(s, 'p', i);
        if (!p || !p->visible)
            continue;
        if (in_rect(s, p, x, y)) {
            if (i == pi) {
                puzzle_place(s, pi, p);
                placed = true;
            } else {
                revert_piece_to_tray(s, piece);
                piece->left = piece->home_left;
                piece->top = piece->home_top;
                play_bad(s);
            }
            break;
        }
    }
    if (!placed && !piece->placed) {
        piece->left = piece->home_left;
        piece->top = piece->home_top;
    }
    s->drag_idx = -1;
    s->selected_idx = -1;
}

bool modules_click(Scene *s, int x, int y, Comp *hit)
{
    if (s->module >= 10) {
        if (hit && str_ieq(hit->name, "kReset")) {
            hit->hover_wave_pending = false;
            reset_game(s);
            /* Keep the pencil in its pressed state while the pointer is still
               on it, otherwise the next hover frame would queue its phrase and
               cut the rules cue.  Moving away and back re-arms the hint. */
            hit->state = ST_ABNORMAL;
            play_rules_cue(s);
            if (getenv("ABC_DEBUG"))
                fprintf(stderr, "reset kReset -> rules cue\n");
            return true;
        }
        if (hit && str_ieq(hit->name, "kPrint")) {
            comp_play_click(s, hit);
            s->print_requested = true;
            return true;
        }
    }

    switch (s->module) {
    case 0:
        scene_request(s, 1001);
        return true;
    case 1:
        if (hit && str_ieq(hit->name, "info"))
            return train_click(s, x, y, hit);
        return train_click(s, x, y, hit);
    case 3:
        return tv_click(s, x, y, hit);
    case 10:
        return coloring_click(s, x, y, hit);
    case 11:
        return puzzle_click(s, x, y, hit);
    case 12:
        return skladanka_click(s, x, y, hit);
    case 13:
        return szczegoly_click(s, x, y, hit);
    case 15:
        return zg1_click(s, x, y, hit);
    case 16:
        return zg2_click(s, x, y, hit);
    case 17:
        return zg3_click(s, x, y, hit);
    case 18:
        return zg4_click(s, x, y, hit);
    default:
        return false;
    }
}

static void outline_rect(SDL_Renderer *r, SDL_Rect rc, Uint8 cr, Uint8 cg, Uint8 cb, int thick)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 200);
    for (int i = 0; i < thick; i++) {
        SDL_FRect q = {(float)(rc.x + i), (float)(rc.y + i),
                       (float)(rc.w - 2 * i), (float)(rc.h - 2 * i)};
        if (q.w > 0 && q.h > 0)
            SDL_RenderRect(r, &q);
    }
}

void modules_render_overlay(Scene *s)
{
    SDL_Renderer *r = s->renderer;
    for (int i = 0; i < s->ncomps; i++) {
        Comp *c = &s->comps[i];
        if (!c->disabled || !c->visible)
            continue;
        SDL_FRect rc;
        comp_rect(s, c, &rc);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 245, 210, 30, 110);
        SDL_RenderFillRect(r, &rc);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    switch (s->module) {
    case 10:
        if (s->palette_n > 0 && s->sel_color >= 0 && s->sel_color < s->palette_n) {
            Comp *k = find_num(s, 'k', s->sel_color + 1);
            if (k) {
                SDL_FRect rk;
                comp_rect(s, k, &rk);
                outline_rect(r, (SDL_Rect){(int)rk.x, (int)rk.y, (int)rk.w, (int)rk.h},
                             255, 255, 255, 2);
            }
        }
        break;
    case 11:
        if (s->selected_idx > 0) {
            Comp *p = find_num(s, 'k', s->selected_idx);
            if (p) {
                SDL_Rect rc = clamped_rect(s, p);
                outline_rect(r, rc, 255, 240, 80, 2);
            }
        }
        break;
    case 12:
        if (s->selected_idx > 0) {
            Comp *p = find_num(s, 's', s->selected_idx);
            if (p) {
                SDL_Rect rc = clamped_rect(s, p);
                outline_rect(r, rc, 255, 240, 60, 2);
            }
        }
        break;
    case 13:
        for (int i = 1; i <= 10; i++) {
            Comp *a = find_num(s, 'x', i);
            Comp *b = find_num(s, 'y', i);
            if (!a || !a->found)
                continue;
            for (int side = 0; side < 2; side++) {
                Comp *c = side ? b : a;
                if (!c)
                    continue;
                SDL_Rect rc = clamped_rect(s, c);
                rc.x -= 2; rc.y -= 2; rc.w += 4; rc.h += 4;
                Uint8 cr = 255, cg = 40, cb = 40;
                if (c->has_color) {
                    cr = (c->color >> 16) & 0xFF;
                    cg = (c->color >> 8) & 0xFF;
                    cb = c->color & 0xFF;
                }
                outline_rect(r, rc, cr, cg, cb, 3);
            }
        }
        break;
    case 17:
        if (s->selected_idx > 0) {
            Comp *o = find_num(s, 'o', s->selected_idx);
            Comp *z = find_num(s, 'z', s->selected_idx);
            Comp *c = (o && o->state == ST_SPECIAL) ? o : z;
            if (c) {
                SDL_Rect rc = clamped_rect(s, c);
                outline_rect(r, rc, 255, 255, 255, 2);
            }
        }
        break;
    default:
        break;
    }
}
