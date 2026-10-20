#include "video.h"
#include "audio.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#endif

struct VideoPlayer {
#ifdef HAVE_FFMPEG
    AVFormatContext *fmt;
    AVCodecContext *vctx;
    int vstream;
    AVFrame *frame;
    AVFrame *rgba;
    struct SwsContext *sws;
    bool eof;
    bool sent_flush;
    double fps;
    double time_base;
    int64_t first_pts;
    double frame_pts_ms;
    uint64_t start_ms;
    uint64_t next_ms;
    uint64_t frame_ms;
#endif
    SDL_Texture *tex;
    int width, height;
    bool finished;
    bool has_audio;
    MIX_Audio *audio;
};

#ifdef HAVE_FFMPEG

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 255);
    p[1] = (unsigned char)((v >> 8) & 255);
    p[2] = (unsigned char)((v >> 16) & 255);
    p[3] = (unsigned char)((v >> 24) & 255);
}

static unsigned char *make_wav(const unsigned char *pcm, int len, int rate, int channels, int bits)
{
    unsigned char *w = xmalloc((size_t)len + 44);
    memcpy(w, "RIFF", 4);
    put32(w + 4, (uint32_t)(len + 36));
    memcpy(w + 8, "WAVEfmt ", 8);
    put32(w + 16, 16);
    w[20] = 1;                       /* PCM */
    w[21] = 0;
    w[22] = (unsigned char)channels;
    w[23] = 0;
    put32(w + 24, (uint32_t)rate);
    put32(w + 28, (uint32_t)(rate * channels * bits / 8));
    w[32] = (unsigned char)(channels * bits / 8);
    w[33] = 0;
    w[34] = (unsigned char)bits;
    w[35] = 0;
    memcpy(w + 36, "data", 4);
    put32(w + 40, (uint32_t)len);
    memcpy(w + 44, pcm, (size_t)len);
    return w;
}

/* Decode the AVI's audio stream into a WAV blob and hand it to the mixer. */
static MIX_Audio *decode_audio(AVFormatContext *fmt)
{
    if (!audio_enabled)
        return NULL;
    int astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (astream < 0)
        return NULL;
    AVStream *st = fmt->streams[astream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec)
        return NULL;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return NULL;
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
        avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2);
    SwrContext *swr = NULL;
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, 44100,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
                            0, NULL) < 0 || !swr || swr_init(swr) < 0) {
        if (swr)
            swr_free(&swr);
        avcodec_free_context(&ctx);
        return NULL;
    }

    unsigned char *pcm = NULL;
    int pcm_len = 0, pcm_cap = 0;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *af = av_frame_alloc();

    for (;;) {
        int ret = av_read_frame(fmt, pkt);
        if (ret < 0)
            break;
        if (pkt->stream_index == astream) {
            avcodec_send_packet(ctx, pkt);
            while (avcodec_receive_frame(ctx, af) == 0) {
                int out_max = swr_get_out_samples(swr, af->nb_samples);
                if (out_max < 0)
                    out_max = af->nb_samples + 64;
                int bytes = out_max * 2 * (int)sizeof(int16_t);
                if (pcm_len + bytes > pcm_cap) {
                    pcm_cap = (pcm_len + bytes) * 2;
                    pcm = xrealloc(pcm, (size_t)pcm_cap);
                }
                uint8_t *out = pcm + pcm_len;
                int got = swr_convert(swr, &out, out_max,
                                      (const uint8_t **)af->extended_data,
                                      af->nb_samples);
                if (got > 0)
                    pcm_len += got * 2 * (int)sizeof(int16_t);
                av_frame_unref(af);
            }
        }
        av_packet_unref(pkt);
    }
    /* flush */
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, af) == 0) {
        int out_max = swr_get_out_samples(swr, af->nb_samples);
        if (out_max > 0) {
            int bytes = out_max * 2 * (int)sizeof(int16_t);
            if (pcm_len + bytes > pcm_cap) {
                pcm_cap = (pcm_len + bytes) * 2;
                pcm = xrealloc(pcm, (size_t)pcm_cap);
            }
            uint8_t *out = pcm + pcm_len;
            int got = swr_convert(swr, &out, out_max,
                                  (const uint8_t **)af->extended_data, af->nb_samples);
            if (got > 0)
                pcm_len += got * 2 * (int)sizeof(int16_t);
        }
        av_frame_unref(af);
    }
    av_frame_free(&af);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    av_channel_layout_uninit(&out_layout);

    MIX_Audio *a = NULL;
    if (pcm_len > 0) {
        unsigned char *wav = make_wav(pcm, pcm_len, 44100, 2, 16);
        a = audio_load_mem(wav, pcm_len + 44);
        free(wav);
    }
    free(pcm);
    return a;
}

static bool decode_next(VideoPlayer *v)
{
    if (v->eof)
        return false;
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return false;
    for (;;) {
        int ret = av_read_frame(v->fmt, pkt);
        if (ret < 0) {
            if (!v->sent_flush) {
                avcodec_send_packet(v->vctx, NULL);
                v->sent_flush = true;
            }
            int got = avcodec_receive_frame(v->vctx, v->frame);
            av_packet_free(&pkt);
            if (got == 0) {
                v->eof = true; /* last frame, will finish on next tick */
                goto convert;
            }
            v->eof = true;
            return false;
        }
        if (pkt->stream_index != v->vstream) {
            av_packet_unref(pkt);
            continue;
        }
        avcodec_send_packet(v->vctx, pkt);
        av_packet_unref(pkt);
        if (avcodec_receive_frame(v->vctx, v->frame) == 0) {
            av_packet_free(&pkt);
            goto convert;
        }
    }
convert:
    {
        int64_t pts = v->frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            pts = v->frame->pts;
        if (pts != AV_NOPTS_VALUE && v->time_base > 0.0) {
            if (v->first_pts == AV_NOPTS_VALUE)
                v->first_pts = pts;
            v->frame_pts_ms = (double)(pts - v->first_pts) * 1000.0 * v->time_base;
        } else {
            v->frame_pts_ms += (double)v->frame_ms;
        }
    }
    sws_scale(v->sws, (const uint8_t *const *)v->frame->data, v->frame->linesize,
              0, v->vctx->height, v->rgba->data, v->rgba->linesize);
    SDL_UpdateTexture(v->tex, NULL, v->rgba->data[0], v->rgba->linesize[0]);
    av_frame_unref(v->frame);
    return true;
}

#endif /* HAVE_FFMPEG */

VideoPlayer *video_open(SDL_Renderer *renderer, const char *path)
{
#ifdef HAVE_FFMPEG
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0)
        return NULL;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return NULL;
    }
    int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vstream < 0) {
        avformat_close_input(&fmt);
        return NULL;
    }
    AVStream *st = fmt->streams[vstream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        log_warn("video %s: no decoder for codec %s", path,
                 avcodec_get_name(st->codecpar->codec_id));
        avformat_close_input(&fmt);
        return NULL;
    }
    AVCodecContext *vctx = avcodec_alloc_context3(codec);
    if (!vctx || avcodec_parameters_to_context(vctx, st->codecpar) < 0 ||
        avcodec_open2(vctx, codec, NULL) < 0) {
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        return NULL;
    }
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgba = av_frame_alloc();
    if (!frame || !rgba) {
        av_frame_free(&frame);
        av_frame_free(&rgba);
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        return NULL;
    }
    int w = vctx->width, h = vctx->height;
    rgba->format = AV_PIX_FMT_RGBA;
    rgba->width = w;
    rgba->height = h;
    if (av_frame_get_buffer(rgba, 0) < 0) {
        av_frame_free(&frame);
        av_frame_free(&rgba);
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        return NULL;
    }
    struct SwsContext *sws = sws_getContext(w, h, vctx->pix_fmt, w, h,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                            NULL, NULL, NULL);
    if (!sws) {
        av_frame_free(&frame);
        av_frame_free(&rgba);
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        return NULL;
    }
    VideoPlayer *v = xcalloc(1, sizeof(*v));
    v->fmt = fmt;
    v->vctx = vctx;
    v->vstream = vstream;
    v->frame = frame;
    v->rgba = rgba;
    v->sws = sws;
    v->width = w;
    v->height = h;
    v->fps = av_q2d(av_guess_frame_rate(fmt, st, NULL));
    if (v->fps < 1.0 || v->fps > 60.0)
        v->fps = 15.0;
    v->frame_ms = (uint64_t)(1000.0 / v->fps + 0.5);
    if (v->frame_ms < 40)
        v->frame_ms = 40;
    v->time_base = av_q2d(st->time_base);
    v->first_pts = AV_NOPTS_VALUE;
    v->frame_pts_ms = 0.0;

    v->audio = decode_audio(fmt);
    v->has_audio = v->audio != NULL;
    /* Audio decoding consumed the file; rewind for video. */
    if (avformat_seek_file(fmt, -1, 0, 0, 0, 0) < 0)
        av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(vctx);

    v->tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                               SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!v->tex) {
        video_close(v);
        return NULL;
    }
    SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_LINEAR);
    if (!decode_next(v)) {
        video_close(v);
        return NULL;
    }
    /* Start the audio and the video clock together.  Subsequent frames are
       scheduled by their PTS, not by a fixed frame counter, so long clips do
       not drift away from the audio. */
    v->start_ms = SDL_GetTicks();
    audio_play_video(v->audio, 1.0f);
    v->next_ms = v->start_ms + v->frame_ms;
    return v;
#else
    (void)renderer;
    (void)path;
    return NULL;
#endif
}

bool video_tick(VideoPlayer *v, uint64_t now_ms)
{
#ifdef HAVE_FFMPEG
    if (!v || v->finished)
        return false;
    if (now_ms < v->next_ms)
        return true;
    if (!decode_next(v)) {
        v->finished = true;
        return false;
    }
    v->next_ms = v->start_ms + (uint64_t)(v->frame_pts_ms + (double)v->frame_ms);
    if (v->next_ms < now_ms)
        v->next_ms = now_ms;
    if (getenv("ABC_DEBUG"))
        fprintf(stderr, "video frame %.0f ms, wall %.0f ms\n",
                v->frame_pts_ms, (double)(now_ms - v->start_ms));
    return true;
#else
    (void)v;
    (void)now_ms;
    return false;
#endif
}

void video_render(VideoPlayer *v, SDL_Renderer *r, SDL_Rect dst)
{
    if (!v)
        return;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_FRect d = {(float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h};
    SDL_RenderFillRect(r, &d);
    if (v->tex)
        SDL_RenderTexture(r, v->tex, NULL, &d);
}

void video_close(VideoPlayer *v)
{
    if (!v)
        return;
    /* Stop the exclusive video soundtrack, then clear any stale one-shots. */
    audio_stop_video();
    audio_stop_all();
#ifdef HAVE_FFMPEG
    if (v->tex)
        SDL_DestroyTexture(v->tex);
    if (v->sws)
        sws_freeContext(v->sws);
    if (v->frame)
        av_frame_free(&v->frame);
    if (v->rgba)
        av_frame_free(&v->rgba);
    if (v->vctx)
        avcodec_free_context(&v->vctx);
    if (v->fmt)
        avformat_close_input(&v->fmt);
#else
    if (v->tex)
        SDL_DestroyTexture(v->tex);
#endif
    if (v->audio)
        audio_free_mem(v->audio);
    free(v);
}

int video_width(const VideoPlayer *v)
{
    return v ? v->width : 0;
}

int video_height(const VideoPlayer *v)
{
    return v ? v->height : 0;
}

bool video_has_audio(const VideoPlayer *v)
{
    return v ? v->has_audio : false;
}
