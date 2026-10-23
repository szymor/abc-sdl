#define _GNU_SOURCE
#include "audio.h"
#include "dfm.h"
#include "font.h"
#include "platform/platform.h"
#include "scene.h"
#include "ui/hud.h"
#include "util.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <dirent.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct {
    bool active;
    SDL_FingerID finger;
    float down_x, down_y;
    int lx, ly;
    bool dragged;
    bool reveal_only;
    bool train_gesture;
    bool hud;
} TouchState;

typedef struct {
    const char *data_root;
    const char *addon_root;
    int start_id;
    int scale;
    bool fullscreen;
    bool mute;
    bool validate;
    int shot_id;
    const char *shot_out;
    const char *script;
} Options;

static bool dir_exists(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return false;
    closedir(d);
    return true;
}

static char *ini_root(void)
{
    size_t len = 0;
    unsigned char *data = read_file("ABC.INI", &len);
    if (!data)
        data = read_file("abc.ini", &len);
    if (!data)
        return NULL;
    char *root = NULL;
    char *save = NULL;
    for (char *line = strtok_r((char *)data, "\r\n", &save); line;
         line = strtok_r(NULL, "\r\n", &save)) {
        while (*line == ' ' || *line == '\t')
            line++;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        while (*val == ' ' || *val == '\t')
            val++;
        if (strcasecmp(key, "Glowny") == 0)
            root = xstrdup(val);
    }
    free(data);
    return root;
}

static const char *pick_data_root(const char *requested)
{
    static char buf[1024];
    if (requested) {
        snprintf(buf, sizeof(buf), "%s", requested);
        return buf;
    }
    const char *platform_root = platform_data_root_default();
    if (platform_root && *platform_root) {
        char probe[1024];
        snprintf(probe, sizeof(probe), "%s/FRM", platform_root);
        if (dir_exists(probe))
            return platform_root;
    }
    const char *env = getenv("ABC_DATA");
    if (env && *env) {
        snprintf(buf, sizeof(buf), "%s", env);
        return buf;
    }
    char *ini = ini_root();
    if (ini && *ini) {
        snprintf(buf, sizeof(buf), "%s", ini);
        free(ini);
        return buf;
    }
    free(ini);
    static const char *candidates[] = {
        "abc",
        "./abc",
        "../abc-re/abc",
        "../../abc-re/abc",
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        char probe[1024];
        snprintf(probe, sizeof(probe), "%s/FRM", candidates[i]);
        if (dir_exists(probe))
            return candidates[i];
    }
    return "abc";
}

static const char *pick_addon_root(const char *requested)
{
    static char buf[2048];
    if (requested && *requested) {
        snprintf(buf, sizeof(buf), "%s", requested);
        return buf;
    }
    const char *platform_root = platform_addon_root_default();
    if (platform_root && *platform_root && dir_exists(platform_root))
        return platform_root;
    const char *env = getenv("ABC_ADDON");
    if (env && *env) {
        snprintf(buf, sizeof(buf), "%s", env);
        return buf;
    }
    if (dir_exists("addon"))
        return "addon";
    const char *base = SDL_GetBasePath();
    if (base) {
        char candidate[1200];
        snprintf(candidate, sizeof(candidate), "%s../addon", base);
        if (dir_exists(candidate)) {
            snprintf(buf, sizeof(buf), "%s", candidate);
            return buf;
        }
        snprintf(candidate, sizeof(candidate), "%saddon", base);
        if (dir_exists(candidate)) {
            snprintf(buf, sizeof(buf), "%s", candidate);
            return buf;
        }
    }
    return "addon";
}

static void usage(void)
{
    puts("Moje Pierwsze ABC - SDL3 re-implementation\n"
         "usage: abc-sdl [options]\n"
         "  --data DIR        content root (contains FRM/ WAV/ AVI/ MIDI/)\n"
         "  --addon DIR       fallback asset root (default: ./addon or ../addon)\n"
         "  --start ID        scene to open (default 100 = splash)\n"
         "  --scale N         integer window scale (default 2)\n"
         "  --fullscreen      start fullscreen\n"
         "  --mute            disable all audio\n"
         "  --validate        parse and validate every scene, then exit\n"
         "  --shot ID         render scene ID to PNG and exit\n"
         "  --out FILE        output file for --shot\n"
         "  --script FILE     replay a click script (move/click/down/up/wait/shot)\n"
         "  --help            this text\n");
}

static bool parse_args(int argc, char **argv, Options *o)
{
    memset(o, 0, sizeof(*o));
    o->start_id = 100;
    o->scale = 2;
    o->shot_id = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--data") == 0 && i + 1 < argc) {
            o->data_root = argv[++i];
        } else if (strcmp(a, "--addon") == 0 && i + 1 < argc) {
            o->addon_root = argv[++i];
        } else if (strcmp(a, "--start") == 0 && i + 1 < argc) {
            o->start_id = atoi(argv[++i]);
        } else if (strcmp(a, "--scale") == 0 && i + 1 < argc) {
            o->scale = atoi(argv[++i]);
            if (o->scale < 1)
                o->scale = 1;
        } else if (strcmp(a, "--fullscreen") == 0) {
            o->fullscreen = true;
        } else if (strcmp(a, "--mute") == 0) {
            o->mute = true;
        } else if (strcmp(a, "--validate") == 0) {
            o->validate = true;
        } else if (strcmp(a, "--shot") == 0 && i + 1 < argc) {
            o->shot_id = atoi(argv[++i]);
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            o->shot_out = argv[++i];
        } else if (strcmp(a, "--script") == 0 && i + 1 < argc) {
            o->script = argv[++i];
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            exit(0);
        } else {
            log_error("unknown argument: %s", a);
            usage();
            return false;
        }
    }
    o->data_root = pick_data_root(o->data_root);
    return true;
}

static int validate(const char *data_root)
{
    char *dirpath = path_join(data_root, "FRM");
    DIR *d = opendir(dirpath);
    if (!d) {
        log_error("cannot open %s", dirpath);
        free(dirpath);
        return 1;
    }
    int files = 0, records = 0, errors = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (!str_iends(e->d_name, ".FRM"))
            continue;
        char *path = path_join(dirpath, e->d_name);
        size_t len = 0;
        unsigned char *buf = read_file(path, &len);
        if (!buf) {
            log_error("cannot read %s", path);
            errors++;
            free(path);
            continue;
        }
        int n = 0;
        DfmComponent **recs = dfm_parse(buf, len, &n);
        if (n == 0) {
            log_error("%s: no records", e->d_name);
            errors++;
        }
        files++;
        records += n;
        dfm_free(recs, n);
        free(buf);
        free(path);
    }
    closedir(d);
    free(dirpath);
    printf("scenes: %d\nstreams: %d\nparse errors: %d\n", files, records, errors);
    if (files != 294)
        log_warn("expected 294 scenes, found %d", files);
    if (records != 6330)
        log_warn("expected 6330 component streams, found %d", records);
    return errors ? 1 : 0;
}

static SDL_Rect fit_rect(SDL_Window *window)
{
    int ww = ABC_W, wh = ABC_H;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    SDL_Rect safe = {0, 0, ww, wh};
    if (!SDL_GetWindowSafeArea(window, &safe) || safe.w <= 0 || safe.h <= 0) {
        safe = (SDL_Rect){0, 0, ww, wh};
    }
    float scale = fminf((float)safe.w / ABC_W, (float)safe.h / ABC_H);
    int w = (int)(ABC_W * scale);
    int h = (int)(ABC_H * scale);
    SDL_Rect r = {safe.x + (safe.w - w) / 2, safe.y + (safe.h - h) / 2, w, h};
    return r;
}

static void map_mouse(const SDL_Rect *dst, float wx, float wy, int *lx, int *ly)
{
    if (dst->w <= 0 || dst->h <= 0) {
        *lx = *ly = -1;
        return;
    }
    *lx = (int)((wx - dst->x) * ABC_W / dst->w);
    *ly = (int)((wy - dst->y) * ABC_H / dst->h);
}

static bool map_touch(SDL_Window *window, const SDL_Rect *dst, float nx, float ny,
                      int *lx, int *ly)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    float wx = nx * (float)ww;
    float wy = ny * (float)wh;
    map_mouse(dst, wx, wy, lx, ly);
    return *lx >= 0 && *ly >= 0 && *lx < ABC_W && *ly < ABC_H;
}

static bool save_png(SDL_Renderer *r, const char *path)
{
    SDL_Surface *surf = SDL_RenderReadPixels(r, NULL);
    if (!surf)
        return false;
    SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!conv)
        return false;
    bool ok = IMG_SavePNG(conv, path);
    SDL_DestroySurface(conv);
    return ok;
}

static char *screenshot_path(int id, const char *tag)
{
    const char *dir = platform_writable_dir();
    mkdir_p(dir);
    static char path[768];
    snprintf(path, sizeof(path), "%s/%s-%d-%llu.png", dir, tag, id,
             (unsigned long long)SDL_GetTicks());
    return path;
}

static int script_navigate(SDL_Renderer *r, const char *root, const char *addon,
                           Scene **ps, int *id)
{
    Scene *s = *ps;
    if (!s->request_pending)
        return 0;
    int next = s->request_id;
    s->request_pending = false;
    if (next == 9999)
        return -1;
    if (next <= 0)
        return 0;
    int mx = s->mouse_x, my = s->mouse_y;
    scene_free(s);
    *id = next;
    s = scene_load(r, root, addon, next);
    if (!s)
        return -1;
    s->mouse_x = mx;
    s->mouse_y = my;
    *ps = s;
    return 1;
}

static int run_script(SDL_Renderer *r, SDL_Texture *canvas, const char *root,
                      const char *addon, int start, const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f) {
        log_error("cannot open script %s", file);
        return 1;
    }
    int id = start;
    Scene *s = scene_load(r, root, addon, id);
    if (!s) {
        fclose(f);
        return 1;
    }
    s->mouse_x = s->mouse_y = -1;
    char line[512];
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p)
            continue;
        char op[32] = {0};
        int x = 0, y = 0, ms = 0;
        char arg[400] = {0};
        int n = sscanf(p, "%31s", op);
        if (n != 1)
            continue;
        if (strcmp(op, "move") == 0 || strcmp(op, "click") == 0 ||
            strcmp(op, "down") == 0 || strcmp(op, "up") == 0) {
            if (sscanf(p, "%*s %d %d", &x, &y) != 2)
                continue;
            s->mouse_x = x;
            s->mouse_y = y;
            if (strcmp(op, "click") == 0) {
                scene_mouse_down(s, x, y);
                scene_click(s, x, y);
                scene_mouse_up(s, x, y);
            } else if (strcmp(op, "down") == 0) {
                scene_mouse_down(s, x, y);
                scene_click(s, x, y);
            } else if (strcmp(op, "up") == 0) {
                scene_mouse_up(s, x, y);
            }
        } else if (strcmp(op, "wait") == 0) {
            if (sscanf(p, "%*s %d", &ms) != 1)
                continue;
            int steps = ms / 16;
            for (int i = 0; i < steps; i++) {
                scene_update(s, 0.016f);
                audio_tick();
                SDL_Delay(16);
                int nr = script_navigate(r, root, addon, &s, &id);
                if (nr < 0) {
                    rc = 0;
                    goto done;
                }
            }
        } else if (strcmp(op, "shot") == 0) {
            if (sscanf(p, "%*s %399s", arg) != 1)
                continue;
            SDL_SetRenderTarget(r, canvas);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            scene_render(s);
            bool ok = save_png(r, arg);
            SDL_SetRenderTarget(r, NULL);
            log_info("script shot %s: %s", arg, ok ? "ok" : SDL_GetError());
        } else if (strcmp(op, "render") == 0) {
            SDL_SetRenderTarget(r, canvas);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            scene_render(s);
            SDL_SetRenderTarget(r, NULL);
        } else if (strcmp(op, "player") == 0) {
            if (sscanf(p, "%*s %d %d", &x, &y) != 2)
                continue;
            scene_set_player(s, x, y);
        } else if (strcmp(op, "dump") == 0) {
            printf("scene=%d found=%d target=%d sel=%d complete=%d player=%d,%d video=%d request=%d scroll=%d\n",
                   s->id, s->found_count, s->target_count, s->selected_idx,
                   (int)s->complete, s->player_x, s->player_y, s->video ? 1 : 0,
                   s->request_pending ? s->request_id : 0, s->scroll);
        } else if (strcmp(op, "scene") == 0) {
            if (sscanf(p, "%*s %d", &x) != 1)
                continue;
            scene_free(s);
            id = x;
            s = scene_load(r, root, addon, id);
            if (!s) {
                rc = 1;
                goto done;
            }
            s->mouse_x = s->mouse_y = -1;
        }
        scene_update(s, 0.016f);
        if (s->print_requested) {
            SDL_SetRenderTarget(r, canvas);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            scene_render(s);
            char out[160];
            snprintf(out, sizeof(out), "screenshots/script-print-%d-%llu.png",
                     s->id, (unsigned long long)SDL_GetTicks());
            mkdir_p("screenshots");
            bool ok = save_png(r, out);
            SDL_SetRenderTarget(r, NULL);
            log_info("script print %s: %s", out, ok ? "ok" : SDL_GetError());
            s->print_requested = false;
        }
        int nr = script_navigate(r, root, addon, &s, &id);
        if (nr < 0)
            goto done;
    }
done:
    fclose(f);
    if (s)
        scene_free(s);
    return rc;
}

static bool g_touch_input = false;
static bool g_hud_enabled = false;
static bool g_paused = false;
static bool g_sound_on = true;
static bool g_music_on = true;
static uint64_t g_last_back_ms = 0;

static void apply_input_mode(Scene *scene)
{
    if (scene)
        scene_set_touch_mode(scene, g_touch_input);
}

static void get_hud_layout(HudLayout *layout, SDL_Window *window)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    SDL_Rect stage = fit_rect(window);
    hud_layout(layout, ww, wh, stage, g_paused);
}

static void app_back(Scene **ps, int *scene_id, int *hist_n, int *history,
                     bool *running)
{
    Scene *s = *ps;
    if (g_paused) {
        g_paused = false;
        if (g_music_on)
            audio_music_resume();
        return;
    }
    if (!s) {
        *running = false;
        return;
    }
    if (s->info_overlay) {
        s->info_overlay = false;
        return;
    }
    if (s->video) {
        video_close(s->video);
        s->video = NULL;
        s->video_stop_music = false;
        if (g_music_on && g_sound_on)
            audio_music_resume();
        return;
    }
    if (*hist_n > 0) {
        int prev = history[--(*hist_n)];
        scene_free(s);
        *ps = NULL;
        *scene_id = prev;
        return;
    }
    uint64_t now = SDL_GetTicks();
    if (now - g_last_back_ms < 2000) {
        *running = false;
    } else {
        g_last_back_ms = now;
        scene_toast(s, "NACISNIJ PONOWNIE ABY WYJSC", 1800);
    }
}

static void enter_pause(Scene *scene)
{
    if (g_paused)
        return;
    log_info("pause entered");
    g_paused = true;
    if (scene && scene->module == 1)
        modules_save_train_scroll(scene);
    audio_music_pause();
    audio_stop_all();
    if (scene && scene->video) {
        video_close(scene->video);
        scene->video = NULL;
    }
}

static void leave_pause(void)
{
    if (!g_paused)
        return;
    log_info("pause left");
    g_paused = false;
    if (g_music_on && g_sound_on)
        audio_music_resume();
}

static bool handle_hud_action(HudAction action, Scene **ps, int *scene_id,
                              int *hist_n, int *history, bool *running)
{
    Scene *s = *ps;
    log_info("hud action %d", (int)action);
    switch (action) {
    case HUD_PAUSE:
        enter_pause(s);
        return true;
    case HUD_HOME:
        if (s) {
            scene_request(s, 1001);
            leave_pause();
        }
        return true;
    case HUD_SOUND:
    case HUD_EFFECTS:
        g_sound_on = !g_sound_on;
        audio_enabled = g_sound_on;
        if (!g_sound_on) {
            audio_stop_all();
            audio_music_pause();
        } else if (!g_paused && g_music_on) {
            audio_music_resume();
        }
        return true;
    case HUD_BACK:
        app_back(ps, scene_id, hist_n, history, running);
        return true;
    case HUD_RESUME:
        leave_pause();
        return true;
    case HUD_MUSIC:
        g_music_on = !g_music_on;
        if (!g_music_on)
            audio_music_pause();
        else if (!g_paused && g_sound_on)
            audio_music_resume();
        return true;
    case HUD_MANAGER:
        enter_pause(s);
        platform_open_content_manager();
        return true;
    case HUD_EXIT:
        *running = false;
        return true;
    default:
        return false;
    }
}

#ifdef __ANDROID__
int SDL_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    Options opt;
    if (!parse_args(argc, argv, &opt))
        return 1;

    if (opt.validate)
        return validate(opt.data_root);

    if (platform_is_android()) {
        /* Touch arrives through SDL_EVENT_FINGER_*; disable synthetic mouse
           events so a tap is not handled twice. */
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        log_error("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_DisableScreenSaver();

    int ww = ABC_W * opt.scale, wh = ABC_H * opt.scale;
    SDL_WindowFlags wflags = SDL_WINDOW_RESIZABLE;
    if (opt.shot_id >= 0 || opt.script)
        wflags |= SDL_WINDOW_HIDDEN;
    if (opt.fullscreen)
        wflags |= SDL_WINDOW_FULLSCREEN;
    if (platform_is_android()) {
        wflags = SDL_WINDOW_FULLSCREEN;
        ww = ABC_W;
        wh = ABC_H;
    }
    SDL_Window *window = SDL_CreateWindow("Moje Pierwsze ABC", ww, wh, wflags);
    if (!window) {
        log_error("cannot create window: %s", SDL_GetError());
        return 1;
    }
    if (!platform_is_android()) {
        SDL_SetWindowMinimumSize(window, 320, 240);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        log_error("cannot create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_Texture *canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_TARGET, ABC_W, ABC_H);
    if (!canvas) {
        log_error("cannot create canvas: %s", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);

    if (!opt.mute)
        audio_init();

    opt.addon_root = pick_addon_root(opt.addon_root);
    log_info("data root: %s", opt.data_root);
    log_info("addon root: %s%s", opt.addon_root,
             dir_exists(opt.addon_root) ? "" : " (missing)");

    if (opt.script) {
        int rc = run_script(renderer, canvas, opt.data_root, opt.addon_root,
                            opt.start_id, opt.script);
        SDL_DestroyTexture(canvas);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        audio_shutdown();
        SDL_Quit();
        return rc;
    }

    if (opt.shot_id >= 0) {
        Scene *s = scene_load(renderer, opt.data_root, opt.addon_root, opt.shot_id);
        if (!s) {
            SDL_DestroyTexture(canvas);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        s->mouse_x = s->mouse_y = -1;
        scene_update(s, 0.016f);
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        scene_render(s);
        char *out = (char *)opt.shot_out;
        if (!out)
            out = screenshot_path(opt.shot_id, "scene");
        bool ok = save_png(renderer, out);
        log_info("wrote %s: %s", out, ok ? "ok" : SDL_GetError());
        scene_free(s);
        SDL_DestroyTexture(canvas);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        audio_shutdown();
        SDL_Quit();
        return ok ? 0 : 1;
    }

    int history[64];
    int hist_n = 0;
    bool running = true;
    int scene_id = opt.start_id;
    Scene *scene = NULL;
    TouchState touch = {0};
    g_touch_input = platform_is_android();
    g_hud_enabled = platform_is_android();
    g_sound_on = !opt.mute;
    uint64_t last = SDL_GetTicksNS();

    while (running) {
        if (!scene) {
            scene = scene_load(renderer, opt.data_root, opt.addon_root, scene_id);
            if (!scene) {
                log_error("cannot open scene %d", scene_id);
                break;
            }
            apply_input_mode(scene);
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                enter_pause(scene);
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                /* Stay on the pause screen; the child resumes explicitly. */
                break;
            case SDL_EVENT_TERMINATING:
                if (scene && scene->module == 1)
                    modules_save_train_scroll(scene);
                running = false;
                break;
            case SDL_EVENT_LOW_MEMORY:
                audio_stop_all();
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET: {
                /* Android can lose the GL context while the app is hidden.
                   SDL textures are no longer valid; rebuild the canvas and
                   reload the current scene from the disc tree. */
                if (canvas)
                    SDL_DestroyTexture(canvas);
                canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_TARGET, ABC_W, ABC_H);
                if (canvas)
                    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
                if (scene) {
                    scene_free(scene);
                    scene = NULL;
                }
                break;
            }
            case SDL_EVENT_FINGER_DOWN: {
                if (touch.active || !scene)
                    break;
                int wwp = 0, whp = 0;
                SDL_GetWindowSizeInPixels(window, &wwp, &whp);
                float wxp = ev.tfinger.x * (float)wwp;
                float wyp = ev.tfinger.y * (float)whp;
                if (g_hud_enabled) {
                    HudLayout hl;
                    get_hud_layout(&hl, window);
                    HudAction ha = hud_hit(&hl, wxp, wyp);
                    if (ha != HUD_NONE) {
                        handle_hud_action(ha, &scene, &scene_id, &hist_n, history, &running);
                        touch.active = true;
                        touch.finger = ev.tfinger.fingerID;
                        touch.hud = true;
                        break;
                    }
                    if (g_paused) {
                        touch.active = true;
                        touch.finger = ev.tfinger.fingerID;
                        touch.hud = true;
                        break;
                    }
                }
                SDL_Rect dst = fit_rect(window);
                int lx = -1, ly = -1;
                if (!map_touch(window, &dst, ev.tfinger.x, ev.tfinger.y, &lx, &ly))
                    break;
                touch.active = true;
                touch.finger = ev.tfinger.fingerID;
                log_info("touch down %d,%d", lx, ly);
                touch.lx = lx;
                touch.ly = ly;
                int ww = 0, wh = 0;
                SDL_GetWindowSizeInPixels(window, &ww, &wh);
                touch.down_x = ev.tfinger.x * (float)ww;
                touch.down_y = ev.tfinger.y * (float)wh;
                touch.dragged = false;
                touch.reveal_only = false;
                touch.train_gesture = false;
                touch.hud = false;
                g_touch_input = true;
                scene_set_touch_mode(scene, true);
                if (scene->module == 2) {
                    /* Reveal-first: the first tap on a hidden object only
                       reveals it; the next tap opens the word. */
                    touch.reveal_only = scene_touch_reveal(scene, lx, ly);
                }
                if (!touch.reveal_only) {
                    scene->mouse_x = lx;
                    scene->mouse_y = ly;
                    scene_mouse_down(scene, lx, ly);
                    touch.train_gesture = modules_train_touch_down(scene, lx, ly);
                    if (scene->module == 14)
                        scene_set_maze_target(scene, lx, ly);
                }
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                if (!touch.active || ev.tfinger.fingerID != touch.finger || !scene)
                    break;
                if (touch.hud || g_paused || touch.reveal_only)
                    break;
                SDL_Rect dst = fit_rect(window);
                int lx = -1, ly = -1;
                if (!map_touch(window, &dst, ev.tfinger.x, ev.tfinger.y, &lx, &ly))
                    break;
                int ww = 0, wh = 0;
                SDL_GetWindowSizeInPixels(window, &ww, &wh);
                float wx = ev.tfinger.x * (float)ww;
                float wy = ev.tfinger.y * (float)wh;
                float dx = wx - touch.down_x;
                float dy = wy - touch.down_y;
                if (dx * dx + dy * dy > 12.0f * 12.0f)
                    touch.dragged = true;
                touch.lx = lx;
                touch.ly = ly;
                scene->mouse_x = lx;
                scene->mouse_y = ly;
                if (touch.train_gesture)
                    modules_train_touch_move(scene, lx, ly);
                if (scene->module == 14)
                    scene_set_maze_target(scene, lx, ly);
                break;
            }
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED: {
                if (!touch.active || ev.tfinger.fingerID != touch.finger || !scene)
                    break;
                if (touch.hud) {
                    touch.hud = false;
                    touch.active = false;
                    break;
                }
                if (g_paused) {
                    touch.active = false;
                    break;
                }
                int lx = touch.lx, ly = touch.ly;
                log_info("touch up %d,%d dragged=%d reveal=%d train=%d", lx, ly,
                         (int)touch.dragged, (int)touch.reveal_only, (int)touch.train_gesture);
                if (!touch.reveal_only && !touch.dragged) {
                    if (touch.train_gesture) {
                        modules_train_touch_up(scene, lx, ly);
                        scene_mouse_up(scene, lx, ly);
                    } else {
                        scene_click(scene, lx, ly);
                        scene_mouse_up(scene, lx, ly);
                    }
                } else {
                    if (touch.train_gesture) {
                        modules_train_touch_up(scene, lx, ly);
                        scene_mouse_up(scene, lx, ly);
                    } else {
                        scene_mouse_up(scene, lx, ly);
                    }
                }
                /* Finger lifted: there is no hover pointer any more.  Keep
                   an explicit preview frame alive through touch_preview_until. */
                scene->mouse_x = -1;
                scene->mouse_y = -1;
                touch.active = false;
                touch.reveal_only = false;
                touch.train_gesture = false;
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_AC_BACK) {
                    app_back(&scene, &scene_id, &hist_n, history, &running);
                } else if (ev.key.key == SDLK_F11) {
                    bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window, !fs);
                } else if (ev.key.key == SDLK_F12) {
                    scene_toast(scene, "ZAPISANO ZRZUT EKRANU", 1800);
                    scene->print_requested = true;
                } else if (ev.key.key == SDLK_LEFT && scene->module == 1) {
                    scene->scroll -= 120;
                    if (scene->scroll < 0) scene->scroll = 0;
                } else if (ev.key.key == SDLK_RIGHT && scene->module == 1) {
                    scene->scroll += 120;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                int x, y;
                if (g_paused)
                    break;
                g_touch_input = false;
                scene_set_touch_mode(scene, false);
                SDL_Rect dst = fit_rect(window);
                map_mouse(&dst, ev.motion.x, ev.motion.y, &x, &y);
                scene->mouse_x = x;
                scene->mouse_y = y;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (g_hud_enabled) {
                        HudLayout hl;
                        get_hud_layout(&hl, window);
                        HudAction ha = hud_hit(&hl, ev.button.x, ev.button.y);
                        if (ha != HUD_NONE) {
                            handle_hud_action(ha, &scene, &scene_id, &hist_n, history, &running);
                            break;
                        }
                        if (g_paused)
                            break;
                    }
                    int x, y;
                    SDL_Rect dst = fit_rect(window);
                    map_mouse(&dst, ev.button.x, ev.button.y, &x, &y);
                    scene->mouse_x = x;
                    scene->mouse_y = y;
                    scene_mouse_down(scene, x, y);
                    scene_click(scene, x, y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT && !g_paused) {
                    int x, y;
                    SDL_Rect dst = fit_rect(window);
                    map_mouse(&dst, ev.button.x, ev.button.y, &x, &y);
                    scene_mouse_up(scene, x, y);
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (scene->module == 1) {
                    scene->scroll += (int)(ev.wheel.y * 60);
                    if (scene->scroll < 0) scene->scroll = 0;
                }
                break;
            default:
                break;
            }
        }

        uint64_t now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        if (dt > 0.1f)
            dt = 0.1f;

        if (platform_audio_focus_lost()) {
            platform_clear_audio_focus_lost();
            enter_pause(scene);
        }
        if (!g_paused)
            scene_update(scene, dt);
        audio_tick();

        /* Draw to the logical 640x480 canvas. */
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        scene_render(scene);

        if (scene->print_requested) {
            char *out = screenshot_path(scene->id, "print");
            bool ok = save_png(renderer, out);
            if (ok) {
                platform_share_file(out);
                scene_toast(scene, "ZAPISANO OBRAZ DO PLIKU PNG", 2200);
            } else
                scene_toast(scene, "NIE UDALO SIE ZAPISAC OBRAZU", 2200);
            scene->print_requested = false;
        }

        /* Present with aspect-preserving letterbox. */
        SDL_SetRenderTarget(renderer, NULL);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Rect dst = fit_rect(window);
        SDL_RenderTexture(renderer, canvas, NULL, &(SDL_FRect){(float)dst.x, (float)dst.y,
                                                               (float)dst.w, (float)dst.h});
        if (g_hud_enabled) {
            HudLayout hl;
            get_hud_layout(&hl, window);
            char title[48];
            snprintf(title, sizeof(title), "SCENA %d", scene ? scene->id : 0);
            hud_draw(renderer, &hl, g_paused, g_sound_on, g_music_on, title);
        }
        SDL_RenderPresent(renderer);

        if (scene->request_pending) {
            int next = scene->request_id;
            scene->request_pending = false;
            if (next == 9999) {
                running = false;
            } else if (next > 0) {
                if (hist_n < 64)
                    history[hist_n++] = scene->id;
                scene_free(scene);
                scene = NULL;
                scene_id = next;
            }
        }

        /* ~60 fps when vsync is unavailable. */
        uint64_t frame_ns = SDL_GetTicksNS() - now;
        if (frame_ns < 16000000ull)
            SDL_DelayNS(16000000ull - frame_ns);
    }

    if (scene)
        scene_free(scene);
    scene_shutdown_cursors();
    audio_music_stop();
    audio_shutdown();
    SDL_DestroyTexture(canvas);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
