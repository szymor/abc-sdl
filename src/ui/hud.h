#ifndef ABC_HUD_H
#define ABC_HUD_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum {
    HUD_NONE = 0,
    HUD_PAUSE,
    HUD_HOME,
    HUD_SOUND,
    HUD_BACK,
    HUD_RESUME,
    HUD_MUSIC,
    HUD_EFFECTS,
    HUD_MANAGER,
    HUD_EXIT
} HudAction;

typedef struct {
    SDL_Rect pause;
    SDL_Rect home;
    SDL_Rect sound;
    SDL_Rect back;
    SDL_Rect rail_bg;
    bool rail;

    SDL_Rect panel;
    SDL_Rect menu[6];
    int menu_count;
} HudLayout;

void hud_layout(HudLayout *layout, int window_w, int window_h, SDL_Rect stage,
                bool paused);
HudAction hud_hit(const HudLayout *layout, float x, float y);
void hud_draw(SDL_Renderer *renderer, const HudLayout *layout, bool paused,
              bool sound_on, bool music_on, const char *scene_title);

#endif
