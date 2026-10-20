#ifndef ABC_VIDEO_H
#define ABC_VIDEO_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct VideoPlayer VideoPlayer;

/* Open an AVI and decode its first frame. Returns NULL if the codec is not
   available or the file cannot be opened. Audio is decoded once and started
   immediately (unless the audio subsystem is disabled). */
VideoPlayer *video_open(SDL_Renderer *renderer, const char *path);

/* Advance playback. Returns false once the clip has finished. */
bool video_tick(VideoPlayer *v, uint64_t now_ms);

void video_render(VideoPlayer *v, SDL_Renderer *r, SDL_Rect dst);
void video_close(VideoPlayer *v);

int video_width(const VideoPlayer *v);
int video_height(const VideoPlayer *v);
bool video_has_audio(const VideoPlayer *v);

#endif
