#include "hud.h"
#include "font.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void fill(SDL_Renderer *r, SDL_Rect rc, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_FRect f = {(float)rc.x, (float)rc.y, (float)rc.w, (float)rc.h};
    SDL_RenderFillRect(r, &f);
}

static void frame(SDL_Renderer *r, SDL_Rect rc, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_FRect f = {(float)rc.x, (float)rc.y, (float)rc.w, (float)rc.h};
    SDL_RenderRect(r, &f);
}

static void label(SDL_Renderer *r, SDL_Rect rc, const char *text, int scale,
                  Uint8 cr, Uint8 cg, Uint8 cb)
{
    int tw = font_text_width(text, scale);
    int th = font_text_height(scale);
    float x = rc.x + (rc.w - tw) * 0.5f;
    float y = rc.y + (rc.h - th) * 0.5f;
    font_draw(r, x, y, text, scale, cr, cg, cb);
}

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void set_rect(SDL_Rect *r, int x, int y, int w, int h)
{
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
}

void hud_layout(HudLayout *layout, int window_w, int window_h, SDL_Rect stage,
                bool paused)
{
    memset(layout, 0, sizeof(*layout));
    int right_bar = window_w - (stage.x + stage.w);
    int left_bar = stage.x;
    int bar = right_bar > left_bar ? right_bar : left_bar;
    bool right = right_bar >= left_bar;
    int size = clamp_int((int)(stage.h * 0.078f), 44, 88);
    int gap = clamp_int(size / 4, 8, 18);

    if (bar >= size + 2 * gap && stage.w > 0) {
        layout->rail = true;
        int x = right ? stage.x + stage.w + (right_bar - size) / 2
                      : (left_bar - size) / 2;
        int total = size * 4 + gap * 3;
        int y = stage.y + (stage.h - total) / 2;
        set_rect(&layout->pause, x, y, size, size);
        set_rect(&layout->home, x, y + size + gap, size, size);
        set_rect(&layout->sound, x, y + (size + gap) * 2, size, size);
        set_rect(&layout->back, x, y + (size + gap) * 3, size, size);
        set_rect(&layout->rail_bg, x - gap, y - gap, size + 2 * gap, total + 2 * gap);
    } else {
        layout->rail = false;
        int small = clamp_int((int)(stage.h * 0.062f), 36, 62);
        int sg = clamp_int(small / 5, 6, 12);
        int total = small * 4 + sg * 3;
        int x = stage.x + stage.w - total - sg;
        int y = stage.y + sg;
        set_rect(&layout->pause, x, y, small, small);
        set_rect(&layout->home, x + small + sg, y, small, small);
        set_rect(&layout->sound, x + (small + sg) * 2, y, small, small);
        set_rect(&layout->back, x + (small + sg) * 3, y, small, small);
        set_rect(&layout->rail_bg, x - sg, y - sg, total + 2 * sg, small + 2 * sg);
    }

    if (paused) {
        int pw = clamp_int((int)(window_w * 0.62f), 360, 760);
        int ph = clamp_int((int)(window_h * 0.82f), 360, 620);
        set_rect(&layout->panel, (window_w - pw) / 2, (window_h - ph) / 2, pw, ph);
        int bx = layout->panel.x + 32;
        int bw = layout->panel.w - 64;
        int bh = clamp_int((int)(window_h * 0.075f), 44, 64);
        int bg = clamp_int(bh / 4, 8, 16);
        int y = layout->panel.y + clamp_int((int)(window_h * 0.13f), 64, 110);
        layout->menu_count = 6;
        for (int i = 0; i < layout->menu_count; i++)
            set_rect(&layout->menu[i], bx, y + i * (bh + bg), bw, bh);
    }
}

HudAction hud_hit(const HudLayout *layout, float x, float y)
{
    if (layout->panel.w > 0) {
        static const HudAction menu_actions[6] = {
            HUD_RESUME, HUD_MUSIC, HUD_EFFECTS, HUD_HOME, HUD_MANAGER, HUD_EXIT
        };
        for (int i = 0; i < layout->menu_count && i < 6; i++) {
            SDL_Rect r = layout->menu[i];
            if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
                return menu_actions[i];
        }
        return HUD_NONE;
    }
    SDL_Rect buttons[4] = {layout->pause, layout->home, layout->sound, layout->back};
    const HudAction actions[4] = {HUD_PAUSE, HUD_HOME, HUD_SOUND, HUD_BACK};
    for (int i = 0; i < 4; i++) {
        SDL_Rect r = buttons[i];
        if (r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
            return actions[i];
    }
    return HUD_NONE;
}

static void button(SDL_Renderer *r, SDL_Rect rc, const char *text, int scale,
                   bool active)
{
    fill(r, rc, active ? 34 : 22, active ? 44 : 30, active ? 54 : 38, 235);
    frame(r, rc, 255, 213, 79, 220);
    label(r, rc, text, scale, 255, 238, 170);
}

void hud_draw(SDL_Renderer *r, const HudLayout *layout, bool paused,
              bool sound_on, bool music_on, const char *scene_title)
{
    if (layout->panel.w > 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        fill(r, (SDL_Rect){0, 0, 100000, 100000}, 0, 0, 0, 165);
        fill(r, layout->panel, 24, 28, 34, 248);
        frame(r, layout->panel, 255, 213, 79, 230);
        int title_scale = 4;
        int tw = font_text_width("PAUZA", title_scale);
        font_draw(r, layout->panel.x + (layout->panel.w - tw) / 2,
                  layout->panel.y + 22, "PAUZA", title_scale, 255, 224, 120);
        if (scene_title && *scene_title) {
            int st = 2;
            int sw = font_text_width(scene_title, st);
            font_draw(r, layout->panel.x + (layout->panel.w - sw) / 2,
                      layout->panel.y + 58, scene_title, st, 200, 205, 210);
        }
        int scale = clamp_int(layout->menu[0].h / 16, 2, 3);
        button(r, layout->menu[0], "WZNOW", scale, true);
        button(r, layout->menu[1], music_on ? "MUZYKA: WL" : "MUZYKA: WYL", scale, music_on);
        button(r, layout->menu[2], sound_on ? "DZWIEK: WL" : "DZWIEK: WYL", scale, sound_on);
        button(r, layout->menu[3], "CIUCHCIA", scale, false);
        button(r, layout->menu[4], "DANE GRY", scale, false);
        button(r, layout->menu[5], "WYJDZ", scale, false);
        return;
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, layout->rail_bg, 10, 14, 18, 135);
    int scale = clamp_int(layout->pause.h / 18, 2, 3);
    button(r, layout->pause, "II", scale, false);
    button(r, layout->home, "DOM", scale, false);
    button(r, layout->sound, sound_on ? "AUD" : "MUTE", scale, sound_on);
    button(r, layout->back, "<", scale, false);
}
