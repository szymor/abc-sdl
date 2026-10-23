#ifndef ABC_PLATFORM_H
#define ABC_PLATFORM_H

#include <stdbool.h>

/* Default content locations.  Android returns app-private storage; desktop
   returns NULL so the existing CLI/INI/relative-path search is used. */
const char *platform_data_root_default(void);
const char *platform_addon_root_default(void);

/* Directory for screenshots and exported images.  Created on demand. */
const char *platform_writable_dir(void);

bool platform_is_android(void);
void platform_log(const char *fmt, ...);

/* Audio focus notifications from the Java activity. */
bool platform_audio_focus_lost(void);
void platform_clear_audio_focus_lost(void);

/* Ask the Java side to open the content manager, if available. */
void platform_open_content_manager(void);

/* Ask the Java side to save/share an exported file, if available. */
void platform_share_file(const char *path);

/* Short haptic tap (0 = no-op). */
void platform_vibrate(int ms);

#endif
