#include "platform.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

static volatile int g_audio_focus_lost = 0;

bool platform_is_android(void)
{
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

bool platform_audio_focus_lost(void)
{
    return g_audio_focus_lost != 0;
}

void platform_clear_audio_focus_lost(void)
{
    g_audio_focus_lost = 0;
}

void platform_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __android_log_print(ANDROID_LOG_INFO, "abc", "%s", buf);
#else
    fputs("[abc] ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
#endif
    va_end(ap);
}

#ifdef __ANDROID__
static const char *android_storage_root(void)
{
    const char *path = SDL_GetAndroidExternalStoragePath();
    if (!path || !*path)
        path = SDL_GetAndroidInternalStoragePath();
    return path;
}

const char *platform_data_root_default(void)
{
    static char buf[2048];
    const char *root = android_storage_root();
    if (!root)
        return NULL;
    snprintf(buf, sizeof(buf), "%s/abc", root);
    return buf;
}

const char *platform_addon_root_default(void)
{
    static char buf[2048];
    const char *root = android_storage_root();
    if (!root)
        return NULL;
    snprintf(buf, sizeof(buf), "%s/addon", root);
    return buf;
}

const char *platform_writable_dir(void)
{
    static char buf[2048];
    const char *pref = SDL_GetPrefPath("szymor", "abc");
    if (pref && *pref) {
        snprintf(buf, sizeof(buf), "%s/screenshots", pref);
        SDL_CreateDirectory(buf);
        return buf;
    }
    const char *root = android_storage_root();
    if (root) {
        snprintf(buf, sizeof(buf), "%s/screenshots", root);
        SDL_CreateDirectory(buf);
        return buf;
    }
    return "screenshots";
}

void platform_open_content_manager(void)
{
    JNIEnv *jenv = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!jenv || !activity)
        return;
    jclass cls = (*jenv)->GetObjectClass(jenv, activity);
    if (!cls)
        return;
    jmethodID mid = (*jenv)->GetMethodID(jenv, cls, "openContentManagerFromNative", "()V");
    if (mid)
        (*jenv)->CallVoidMethod(jenv, activity, mid);
    if ((*jenv)->ExceptionCheck(jenv))
        (*jenv)->ExceptionClear(jenv);
    (*jenv)->DeleteLocalRef(jenv, cls);
}

void platform_share_file(const char *path)
{
    JNIEnv *jenv = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!jenv || !activity || !path)
        return;
    jclass cls = (*jenv)->GetObjectClass(jenv, activity);
    if (!cls)
        return;
    jmethodID mid = (*jenv)->GetMethodID(jenv, cls, "saveImageToGalleryFromNative",
                                         "(Ljava/lang/String;)V");
    if (mid) {
        jstring jpath = (*jenv)->NewStringUTF(jenv, path);
        (*jenv)->CallVoidMethod(jenv, activity, mid, jpath);
        if ((*jenv)->ExceptionCheck(jenv))
            (*jenv)->ExceptionClear(jenv);
        (*jenv)->DeleteLocalRef(jenv, jpath);
    }
    (*jenv)->DeleteLocalRef(jenv, cls);
}

void platform_vibrate(int ms)
{
    (void)ms;
}

JNIEXPORT void JNICALL
Java_pl_szymor_abc_PlatformBridge_nativeAudioFocus(JNIEnv *env, jclass cls, jboolean has_focus)
{
    (void)env;
    (void)cls;
    g_audio_focus_lost = has_focus ? 0 : 1;
}

#else

const char *platform_data_root_default(void)
{
    return NULL;
}

const char *platform_addon_root_default(void)
{
    return NULL;
}

const char *platform_writable_dir(void)
{
    return "screenshots";
}

void platform_open_content_manager(void)
{
}

void platform_share_file(const char *path)
{
    (void)path;
}

void platform_vibrate(int ms)
{
    (void)ms;
}

#endif
