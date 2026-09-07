/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gdk_android_gl_context.h"
#include "gdk_android_runtime.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <stdbool.h>

#define TAG "gnome-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef struct {
    bool surface_alive;
    ANativeWindow *window;
    int surface_generation;
    int width;
    int height;
} GlContextState;

static GlContextState s_ctx = {
    .surface_alive = false,
    .window = NULL,
    .surface_generation = 0,
    .width = 0,
    .height = 0,
};

void gdk_android_gl_context_surface_created(JNIEnv *env, jobject surface)
{
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);

    if (s_ctx.surface_alive) {
        /* Defensive pairing: a second create without prior destroy still emits a synthetic destroy. */
        LOGI("gl_context:surface_created while previous surface alive, forcing destroy for gen=%d", s_ctx.surface_generation);
        gdk_android_runtime_push_event(GDK_ANDROID_EVENT_SURFACE_DESTROYED, s_ctx.surface_generation, 0);
        s_ctx.surface_alive = false;
        if (s_ctx.window != NULL) {
            ANativeWindow_release(s_ctx.window);
            s_ctx.window = NULL;
        }
    }

    s_ctx.surface_generation++;
    s_ctx.surface_alive = true;
    s_ctx.window = window;

    LOGI("gl_context:surface_created window=%p gen=%d", (void *)window, s_ctx.surface_generation);
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_SURFACE_CREATED, s_ctx.surface_generation, 0);
    /* TODO Tag 2: wire ANativeWindow into GTK4/GDK Android backend. */
}

void gdk_android_gl_context_surface_changed(int width, int height)
{
    s_ctx.width = width;
    s_ctx.height = height;

    LOGI("gl_context:surface_changed %dx%d", width, height);
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_SURFACE_CHANGED, width, height);
}

void gdk_android_gl_context_surface_destroyed(void)
{
    if (!s_ctx.surface_alive) {
        LOGI("gl_context:surface_destroyed ignored (no active surface)");
        return;
    }

    LOGI("gl_context:surface_destroyed gen=%d", s_ctx.surface_generation);
    s_ctx.surface_alive = false;
    if (s_ctx.window != NULL) {
        ANativeWindow_release(s_ctx.window);
        s_ctx.window = NULL;
    }
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_SURFACE_DESTROYED, s_ctx.surface_generation, 0);
}

ANativeWindow *
gdk_android_gl_context_ref_native_window(void)
{
    if (s_ctx.window == NULL) {
        LOGI("gl_context:ref_native_window -> NULL (surface_alive=%d gen=%d)",
             s_ctx.surface_alive, s_ctx.surface_generation);
        return NULL;
    }

    LOGI("gl_context:ref_native_window -> %p (gen=%d)",
         (void *)s_ctx.window, s_ctx.surface_generation);
    ANativeWindow_acquire(s_ctx.window);
    return s_ctx.window;
}

int
gdk_android_gl_context_get_surface_generation(void)
{
    return s_ctx.surface_generation;
}

int
gdk_android_gl_context_get_surface_width(void)
{
    return s_ctx.width;
}

int
gdk_android_gl_context_get_surface_height(void)
{
    return s_ctx.height;
}

bool
gdk_android_gl_context_is_surface_alive(void)
{
    return s_ctx.surface_alive;
}
