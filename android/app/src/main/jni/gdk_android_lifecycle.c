/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gdk_android_lifecycle.h"
#include "gdk_android_runtime.h"

#include <android/log.h>

#define TAG "gnome-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

void gdk_android_lifecycle_on_create(int restore_flags)
{
    LOGI("lifecycle:on_create restore_flags=0x%x", restore_flags);
    gdk_android_runtime_init();
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_LIFECYCLE_CREATE, 0, 0);
    if (restore_flags != 0) {
        gdk_android_runtime_push_event(GDK_ANDROID_EVENT_PROCESS_RESTORE_HINT, restore_flags, 0);
    }
}

void gdk_android_lifecycle_on_pause(void)
{
    LOGI("lifecycle:on_pause");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_LIFECYCLE_PAUSE, 0, 0);
}

void gdk_android_lifecycle_on_resume(void)
{
    LOGI("lifecycle:on_resume");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_LIFECYCLE_RESUME, 0, 0);
}

void gdk_android_lifecycle_on_destroy(void)
{
    LOGI("lifecycle:on_destroy");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_LIFECYCLE_DESTROY, 0, 0);
    gdk_android_runtime_shutdown();
}
