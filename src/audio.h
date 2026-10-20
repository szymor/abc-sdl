#ifndef ABC_AUDIO_H
#define ABC_AUDIO_H

#include <SDL3_mixer/SDL_mixer.h>
#include <stdbool.h>

bool audio_init(void);
void audio_shutdown(void);
void audio_tick(void);

MIX_Audio *audio_load_file(const char *path);
MIX_Audio *audio_load_mem(const void *data, int len);
void audio_free_mem(MIX_Audio *a);
void audio_play(MIX_Audio *a, float gain);      /* speech: stops previous speech */
void audio_play_sfx(MIX_Audio *a, float gain);  /* SFX: may overlap speech */
void audio_stop_all(void);

/* Background MIDI playlist. Stops the previous one if the signature differs.
   Pause/resume keep the current playlist position for video playback. */
void audio_music_play(const char *const *paths, int count, bool random_loop, const char *signature);
void audio_music_stop(void);
void audio_music_pause(void);
bool audio_music_resume(void);
const char *audio_music_signature(void);

extern bool audio_enabled;

#endif
