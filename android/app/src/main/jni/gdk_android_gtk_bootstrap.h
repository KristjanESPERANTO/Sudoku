/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef GDK_ANDROID_GTK_BOOTSTRAP_H
#define GDK_ANDROID_GTK_BOOTSTRAP_H

#include "gdk_android_runtime.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gdk_android_gtk_bootstrap_handle_event(GdkAndroidEventType type, int arg0, int arg1);
void gdk_android_gtk_bootstrap_seed_test_entry_state(const char *text);
void gdk_android_gtk_bootstrap_seed_test_button_count(unsigned int count);
void gdk_android_gtk_bootstrap_seed_test_toggle_active(bool active);

#ifdef __cplusplus
}
#endif

#endif