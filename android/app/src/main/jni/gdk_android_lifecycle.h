/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef GDK_ANDROID_LIFECYCLE_H
#define GDK_ANDROID_LIFECYCLE_H

#ifdef __cplusplus
extern "C" {
#endif

void gdk_android_lifecycle_on_create(int restore_flags);
void gdk_android_lifecycle_on_pause(void);
void gdk_android_lifecycle_on_resume(void);
void gdk_android_lifecycle_on_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
