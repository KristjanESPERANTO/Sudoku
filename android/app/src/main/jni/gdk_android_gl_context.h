/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef GDK_ANDROID_GL_CONTEXT_H
#define GDK_ANDROID_GL_CONTEXT_H

#include <jni.h>
#include <android/native_window.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gdk_android_gl_context_surface_created(JNIEnv *env, jobject surface);
void gdk_android_gl_context_surface_changed(int width, int height);
void gdk_android_gl_context_surface_destroyed(void);

/*
 * Returns a retained ANativeWindow reference for backend consumers.
 * Caller must release with ANativeWindow_release() when done.
 */
ANativeWindow *gdk_android_gl_context_ref_native_window(void);

/* Surface metadata for synchronization with backend rendering setup. */
int gdk_android_gl_context_get_surface_generation(void);
int gdk_android_gl_context_get_surface_width(void);
int gdk_android_gl_context_get_surface_height(void);
bool gdk_android_gl_context_is_surface_alive(void);

#ifdef __cplusplus
}
#endif

#endif
