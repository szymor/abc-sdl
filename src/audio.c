#include "audio.h"
#include "util.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

bool audio_enabled = false;

static MIX_Mixer *mixer = NULL;
#define AUDIO_TRACKS 24
static MIX_Track *tracks[AUDIO_TRACKS];
static int next_track = 0;

typedef struct {
    char *path;
    MIX_Audio *audio;
} CacheEntry;
static CacheEntry *cache = NULL;
static int cache_count = 0, cache_cap = 0;

static char *music_sig = NULL;

#ifdef HAVE_FLUIDSYNTH
static fluid_settings_t *fs_settings = NULL;
static fluid_synth_t *fs_synth = NULL;
static fluid_player_t *fs_player = NULL;
static SDL_AudioStream *fs_stream = NULL;
static int16_t *fs_buf = NULL;
static int fs_buf_frames = 0;
static bool fs_ok = false;

static void synth_callback(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)userdata;
    (void)total;
    if (!fs_ok || !fs_synth || additional <= 0)
        return;
    int frames = additional / (2 * (int)sizeof(int16_t));
    if (frames <= 0)
        return;
    if (frames > fs_buf_frames)
        frames = fs_buf_frames;
    fluid_synth_write_s16(fs_synth, frames, fs_buf, 0, 2, fs_buf, 1, 2);
    SDL_PutAudioStreamData(stream, fs_buf, frames * 2 * (int)sizeof(int16_t));
}

static const char *pick_soundfont(void)
{
    static char envbuf[1024];
    const char *env = getenv("ABC_SOUNDFONT");
    if (env && *env)
        return env;
    static const char *candidates[] = {
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/SC-55.SoundFont.v1.2b.sf2",
        "/usr/share/soundfonts/freepats-general-midi.sf2",
        "/usr/share/soundfonts/FluidR3_GS.sf2",
        "/usr/share/soundfonts/Roland_SC-88.sf2",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            snprintf(envbuf, sizeof(envbuf), "%s", candidates[i]);
            return envbuf;
        }
    }
    return NULL;
}

static bool synth_init(void)
{
    fluid_set_log_function(FLUID_WARN, NULL, NULL);
    fluid_set_log_function(FLUID_INFO, NULL, NULL);
    fluid_set_log_function(FLUID_DBG, NULL, NULL);
    const char *sf = pick_soundfont();
    if (!sf) {
        log_warn("no soundfont found; MIDI music disabled (set ABC_SOUNDFONT)");
        return false;
    }
    fs_settings = new_fluid_settings();
    if (!fs_settings)
        return false;
    fluid_settings_setnum(fs_settings, "synth.sample-rate", 44100.0);
    fluid_settings_setnum(fs_settings, "synth.gain", 0.5);
    fluid_settings_setint(fs_settings, "synth.verbose", 0);
    fs_synth = new_fluid_synth(fs_settings);
    if (!fs_synth)
        return false;
    if (fluid_synth_sfload(fs_synth, sf, 1) < 0) {
        log_warn("could not load soundfont %s", sf);
        delete_fluid_synth(fs_synth);
        fs_synth = NULL;
        return false;
    }
    fs_buf_frames = 8192;
    fs_buf = xmalloc((size_t)fs_buf_frames * 2 * sizeof(int16_t));
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;
    fs_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, synth_callback, NULL);
    if (!fs_stream) {
        log_warn("cannot open synth audio stream: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(fs_stream);
    fs_ok = true;
    log_info("MIDI music enabled (soundfont: %s)", sf);
    return true;
}

static void synth_shutdown(void)
{
    fs_ok = false;
    if (fs_player) {
        fluid_player_stop(fs_player);
        delete_fluid_player(fs_player);
        fs_player = NULL;
    }
    if (fs_stream) {
        SDL_DestroyAudioStream(fs_stream);
        fs_stream = NULL;
    }
    if (fs_synth) {
        delete_fluid_synth(fs_synth);
        fs_synth = NULL;
    }
    if (fs_settings) {
        delete_fluid_settings(fs_settings);
        fs_settings = NULL;
    }
    free(fs_buf);
    fs_buf = NULL;
}
#endif

bool audio_init(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log_warn("SDL audio unavailable: %s", SDL_GetError());
        return false;
    }
    if (!MIX_Init()) {
        log_warn("SDL_mixer unavailable: %s", SDL_GetError());
        return false;
    }
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!mixer) {
        log_warn("cannot create mixer: %s (running silently)", SDL_GetError());
        MIX_Quit();
        return false;
    }
    for (int i = 0; i < AUDIO_TRACKS; i++)
        tracks[i] = MIX_CreateTrack(mixer);
    audio_enabled = true;
#ifdef HAVE_FLUIDSYNTH
    synth_init();
#endif
    return true;
}

void audio_shutdown(void)
{
#ifdef HAVE_FLUIDSYNTH
    synth_shutdown();
#endif
    for (int i = 0; i < cache_count; i++) {
        free(cache[i].path);
        if (cache[i].audio)
            MIX_DestroyAudio(cache[i].audio);
    }
    free(cache);
    cache = NULL;
    cache_count = cache_cap = 0;
    if (mixer) {
        for (int i = 0; i < AUDIO_TRACKS; i++)
            if (tracks[i])
                MIX_DestroyTrack(tracks[i]);
        MIX_DestroyMixer(mixer);
        mixer = NULL;
    }
    if (audio_enabled) {
        MIX_Quit();
        audio_enabled = false;
    }
    free(music_sig);
    music_sig = NULL;
}

MIX_Audio *audio_load_file(const char *path)
{
    if (!audio_enabled || !path || !*path)
        return NULL;
    for (int i = 0; i < cache_count; i++)
        if (strcmp(cache[i].path, path) == 0)
            return cache[i].audio;
    MIX_Audio *a = MIX_LoadAudio(mixer, path, true);
    if (!a) {
        log_warn("cannot load audio %s: %s", path, SDL_GetError());
        return NULL;
    }
    if (cache_count == cache_cap) {
        cache_cap = cache_cap ? cache_cap * 2 : 32;
        cache = xrealloc(cache, (size_t)cache_cap * sizeof(*cache));
    }
    cache[cache_count].path = xstrdup(path);
    cache[cache_count].audio = a;
    cache_count++;
    return a;
}

MIX_Audio *audio_load_mem(const void *data, int len)
{
    if (!audio_enabled || !data || len <= 0)
        return NULL;
    SDL_IOStream *io = SDL_IOFromConstMem(data, (size_t)len);
    if (!io)
        return NULL;
    MIX_Audio *a = MIX_LoadAudio_IO(mixer, io, true, true);
    if (!a)
        log_warn("cannot decode embedded audio: %s", SDL_GetError());
    return a;
}

void audio_free_mem(MIX_Audio *a)
{
    if (a)
        MIX_DestroyAudio(a);
}

void audio_play(MIX_Audio *a, float gain)
{
    if (!audio_enabled || !a)
        return;
    if (gain <= 0.0f)
        gain = 1.0f;
    /* Only one spoken cue at a time.  The original uses several players, but
       the child should never hear the rules explanation and an item cue
       simultaneously; starting a new cue interrupts the previous one.
       MIDI playlists are managed separately and are not affected. */
    audio_stop_all();
    int pick = next_track;
    next_track = (next_track + 1) % AUDIO_TRACKS;
    if (getenv("ABC_DEBUG")) {
        SDL_AudioSpec spec;
        double dur = -1.0;
        if (MIX_GetAudioFormat(a, &spec) && spec.freq > 0)
            dur = (double)MIX_GetAudioDuration(a) / (double)spec.freq;
        log_info("audio cue (%.1f s), previous cue stopped", dur);
    }
    MIX_SetTrackGain(tracks[pick], gain);
    MIX_SetTrackAudio(tracks[pick], a);
    MIX_PlayTrack(tracks[pick], 0);
}

void audio_play_sfx(MIX_Audio *a, float gain)
{
    if (!audio_enabled || !a)
        return;
    if (gain <= 0.0f)
        gain = 1.0f;
    /* Non-speech effects (wrong-answer buzz, counters) may play on top of a
       spoken explanation.  Find an idle track; never stop the speech. */
    int pick = -1;
    for (int i = 0; i < AUDIO_TRACKS; i++) {
        int idx = (next_track + i) % AUDIO_TRACKS;
        if (tracks[idx] && MIX_GetTrackRemaining(tracks[idx]) <= 0) {
            pick = idx;
            break;
        }
    }
    if (pick < 0)
        pick = next_track;
    next_track = (pick + 1) % AUDIO_TRACKS;
    if (getenv("ABC_DEBUG"))
        log_info("audio sfx (overlaps speech)");
    MIX_SetTrackGain(tracks[pick], gain);
    MIX_SetTrackAudio(tracks[pick], a);
    MIX_PlayTrack(tracks[pick], 0);
}

void audio_stop_all(void)
{
    if (!audio_enabled)
        return;
    for (int i = 0; i < AUDIO_TRACKS; i++) {
        if (tracks[i])
            MIX_StopTrack(tracks[i], 0);
    }
}

void audio_tick(void)
{
    (void)0;
}

void audio_music_stop(void)
{
#ifdef HAVE_FLUIDSYNTH
    if (fs_player) {
        fluid_player_stop(fs_player);
        delete_fluid_player(fs_player);
        fs_player = NULL;
    }
#endif
    free(music_sig);
    music_sig = NULL;
}

const char *audio_music_signature(void)
{
    return music_sig;
}

void audio_music_play(const char *const *paths, int count, bool random_loop, const char *signature)
{
    if (!paths || count <= 0)
        return;
    if (music_sig && signature && strcmp(music_sig, signature) == 0)
        return;
#ifdef HAVE_FLUIDSYNTH
    if (!fs_ok) {
        free(music_sig);
        music_sig = signature ? xstrdup(signature) : NULL;
        return;
    }
    if (fs_player) {
        fluid_player_stop(fs_player);
        delete_fluid_player(fs_player);
        fs_player = NULL;
    }
    /* Kill residual notes and buffered audio from the previous playlist. */
    fluid_synth_all_sounds_off(fs_synth, -1);
    if (fs_stream) {
        SDL_LockAudioStream(fs_stream);
        SDL_ClearAudioStream(fs_stream);
        SDL_UnlockAudioStream(fs_stream);
    }
    int order[16];
    if (count > 16)
        count = 16;
    for (int i = 0; i < count; i++)
        order[i] = i;
    if (random_loop) {
        for (int i = count - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    }
    fs_player = new_fluid_player(fs_synth);
    if (!fs_player)
        return;
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (fluid_player_add(fs_player, paths[order[i]]) == FLUID_OK)
            added++;
    }
    if (!added) {
        delete_fluid_player(fs_player);
        fs_player = NULL;
        return;
    }
    fluid_player_set_loop(fs_player, 0);
    fluid_player_play(fs_player);
    log_info("music: %s (%d tracks)", signature ? signature : "?", added);
#else
    (void)random_loop;
#endif
    free(music_sig);
    music_sig = signature ? xstrdup(signature) : NULL;
}
