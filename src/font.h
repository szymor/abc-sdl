#ifndef ABC_FONT_H
#define ABC_FONT_H

#include <SDL3/SDL.h>

int font_text_width(const char *s, int scale);
int font_text_height(int scale);
void font_draw(SDL_Renderer *r, float x, float y, const char *s, int scale, Uint8 cr, Uint8 cg, Uint8 cb);

#endif
