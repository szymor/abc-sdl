#ifndef ABC_SCENE_H
#define ABC_SCENE_H

#include "audio.h"
#include "dfm.h"
#include "video.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define ABC_W 640
#define ABC_H 480

typedef enum {
    CC_UNKNOWN = 0,
    CC_IMAGE,
    CC_ZAMIEN,
    CC_WYSUN,
    CC_HOTSPOT,
    CC_ANIM,
    CC_MAPOW,
    CC_MAPOWANIE,
    CC_TLO_MAPOWANIE,
    CC_EKRAN,
    CC_CHECK,
    CC_CURSORS,
    CC_WAVE,
    CC_MIDI,
    CC_MEDIA,
    CC_LABIRYNT,
    CC_PRZESUN,
    CC_TIMER
} CompClass;

typedef enum { ST_NORMAL = 0, ST_ABNORMAL, ST_SPECIAL } CompState;

typedef struct Comp {
    DfmComponent *src;
    char name[64];
    CompClass cls;
    int z;

    int left, top, width, height;
    bool visible, visible0, enabled, autosize;
    bool align_client;

    int normal, abnormal, special;
    CompState state;
    bool has_color;
    uint32_t color;

    SDL_Texture *tex;
    SDL_Texture *gray_tex;
    int tex_w, tex_h, frame_w, frame_h, frame_count;
    bool key_magenta;
    bool key_corner;

    unsigned char *mask;      /* 1 byte per pixel, 1 = hit */
    int mask_w, mask_h;
    bool mask_shape;          /* mask is a useful silhouette, not a full frame */
    bool mask_mouse, mask_graph;

    bool modal_set;
    int modal_result;
    bool auto_click_exit;
    bool click_to_normal, click_to_abnormal;
    bool move_to_normal, move_to_abnormal;
    bool autoplay_normal, autoplay_abnormal;
    bool wave_stop, wave_auto_click;

    char click_file[300], click_file2[300];
    MIX_Audio *wav_normal, *wav_abnormal;
    MIX_Audio *wav_click, *wav_click2;

    bool is_anim;
    int interval, loop_anim;
    int klatki[64];
    int nklatki;
    int anim_index;
    uint64_t anim_next;
    bool anim_playing;

    int slide_max, slide_speed;

    /* Train strip */
    int box_count;
    SDL_Rect boxes[24];
    int box_targets[24];

    /* Maze */
    int max_speed, entry_x, entry_y;
    unsigned char *collision;
    int col_w, col_h;

    /* Game bookkeeping */
    bool found, placed, selected, wrong, hover_lock, correct, disabled;
    bool hover_wave_pending;
    uint64_t hover_wave_at;
    int home_left, home_top;
    char maphs[8][24];
    int nmaphs;
} Comp;

typedef struct Scene {
    int id, module;
    char data_root[1024];
    DfmComponent **roots;
    int nroots;
    Comp *comps;
    int ncomps;
    int origin_x, origin_y;

    char music_paths[4][600];
    int nmusic;
    char music_sig[640];

    MIX_Audio *wav_ok, *wav_bad, *wav_select, *wav_default, *wav_count;
    MIX_Audio *maze_ok, *maze_bad1, *maze_bad2;

    int target_count, found_count;
    bool no_completion;
    bool complete;
    uint64_t complete_at;
    int complete_delay_ms;
    int return_id;
    bool request_pending;
    int request_id;

    bool has_toolbar;
    float toolbar_anim;

    /* Train strip */
    int scroll;
    float scroll_vel;

    uint64_t now;
    uint64_t start_time;

    /* Colouring book */
    uint32_t *paint_px;
    uint32_t *paint_bg;
    SDL_Texture *paint_tex;
    bool paint_dirty;
    uint32_t palette[16];
    int palette_n;
    int sel_color;
    bool print_requested;

    /* Maze */
    int player_x, player_y;
    bool player_init;

    /* Puzzle / Skladanka / Zgadywanka 3 */
    int selected_idx;
    int selected_group;
    int drag_idx;
    int drag_dx, drag_dy;
    bool drag_moved;

    /* Overlays */
    VideoPlayer *video;
    bool video_fullscreen;
    bool video_stop_music;
    uint64_t video_until;
    char video_label[128];
    SDL_Rect video_rect;
    bool info_overlay;
    uint64_t toast_until;
    char toast[160];

    int mouse_x, mouse_y;
    bool mouse_down;

    SDL_Renderer *renderer;
} Scene;

Scene *scene_load(SDL_Renderer *r, const char *data_root, int id);
void scene_free(Scene *s);
void scene_update(Scene *s, float dt);
void scene_render(Scene *s);
bool scene_click(Scene *s, int x, int y);
void scene_mouse_down(Scene *s, int x, int y);
void scene_mouse_up(Scene *s, int x, int y);
void modules_mouse_down(Scene *s, int x, int y);
void modules_mouse_up(Scene *s, int x, int y);

Comp *scene_find(Scene *s, const char *name);
Comp *scene_find_prefix(Scene *s, const char *prefix);
int scene_hit_index(Scene *s, int x, int y);
Comp *scene_hit(Scene *s, int x, int y);
void comp_rect(Scene *s, Comp *c, SDL_FRect *out);
bool comp_contains(Scene *s, Comp *c, int x, int y);
void comp_play_click(Scene *s, Comp *c);
void comp_set_state(Scene *s, Comp *c, CompState st);
void comp_anim_start(Scene *s, Comp *c, bool show);
void comp_make_gray(Scene *s, Comp *c);
void scene_request(Scene *s, int id);
void scene_complete(Scene *s, int delay_ms);
void scene_toast(Scene *s, const char *msg, int ms);
void scene_set_player(Scene *s, int x, int y);
void scene_shutdown_cursors(void);
void scene_set_fix_content(bool enabled);
uint32_t scene_sample_bmp(DfmComponent *src);
SDL_Surface *scene_bmp_surface(DfmValue *v);

/* modules.c */
void modules_setup(Scene *s, const char *data_root);
void modules_update(Scene *s, float dt);
bool modules_click(Scene *s, int x, int y, Comp *hit);
void modules_render_overlay(Scene *s);
const char *scene_word_title(int word);

#endif
