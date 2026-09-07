/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef GDK_ANDROID_RUNTIME_H
#define GDK_ANDROID_RUNTIME_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GDK_ANDROID_EVENT_LIFECYCLE_CREATE = 1,
    GDK_ANDROID_EVENT_PROCESS_RESTORE_HINT,
    GDK_ANDROID_EVENT_LIFECYCLE_PAUSE,
    GDK_ANDROID_EVENT_LIFECYCLE_RESUME,
    GDK_ANDROID_EVENT_LIFECYCLE_DESTROY,
    GDK_ANDROID_EVENT_ACTIVITY_ATTACHED,
    GDK_ANDROID_EVENT_ACTIVITY_DETACHED,
    GDK_ANDROID_EVENT_SURFACE_CREATED,
    GDK_ANDROID_EVENT_SURFACE_CHANGED,
    GDK_ANDROID_EVENT_SURFACE_DESTROYED,
    GDK_ANDROID_EVENT_INPUT_INIT,
    GDK_ANDROID_EVENT_INPUT_MOTION,
    GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN,
    GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE,
    GDK_ANDROID_EVENT_INPUT_TOUCH_UP,
    GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL,
    GDK_ANDROID_EVENT_IME_SHOW,
    GDK_ANDROID_EVENT_IME_HIDE,
    GDK_ANDROID_EVENT_IME_COMMIT,
} GdkAndroidEventType;

enum {
    GDK_ANDROID_RESTORE_FLAG_SAVED_STATE_PRESENT = 1 << 0,
    GDK_ANDROID_RESTORE_FLAG_PREVIOUS_RUN_ALIVE = 1 << 1,
    GDK_ANDROID_RESTORE_FLAG_MATCHING_SAVED_RUN_ID = 1 << 2,
};

typedef void (*GdkAndroidRuntimeDispatchFn)(
    GdkAndroidEventType type,
    int arg0,
    int arg1,
    void *userdata);

void gdk_android_runtime_init(void);
void gdk_android_runtime_shutdown(void);
void gdk_android_runtime_push_event(GdkAndroidEventType type, int arg0, int arg1);
void gdk_android_runtime_set_dispatcher(GdkAndroidRuntimeDispatchFn fn, void *userdata);
void gdk_android_runtime_set_java_vm(JavaVM *vm);

/*
 * Thread-local JNI environment helpers.
 *
 * gdk_android_runtime_get_env() returns a valid JNIEnv* for the current
 * thread, attaching the thread to the JVM on demand when needed.
 *
 * gdk_android_runtime_clear_thread_env() detaches only when the thread was
 * attached through gdk_android_runtime_get_env().
 */
JNIEnv *gdk_android_runtime_get_env(void);
void    gdk_android_runtime_clear_thread_env(void);

/* GLib integration — only available when built with GDK_ANDROID_USE_GLIB_DISPATCH. */
#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
#include <glib.h>
/**
 * gdk_android_runtime_create_event_source:
 *
 * Creates a GLib #GSource that watches a wakeup pipe and dispatches
 * queued Android runtime events by calling
 * gdk_android_gtk_bootstrap_handle_event().  After the first call a
 * wakeup pipe is created; subsequent calls return additional sources
 * sharing the same pipe.
 *
 * Attach the returned source to the default main context before
 * calling g_main_loop_run() so that Android events are delivered
 * while GTK's frame-clock is running.
 */
GSource *gdk_android_runtime_create_event_source (void);
#endif

#ifdef __cplusplus
}
#endif

#endif