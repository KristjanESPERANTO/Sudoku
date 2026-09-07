/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Sudokug-specific GTK bootstrap.
 *
 * Placeholder implementation for Sudokug on Android.
 * Full game launch + touch handling will be integrated in Phase 2.
 */

#include "gdk_android_gtk_bootstrap.h"
#include "gdk_android_input.h"
#include "gdk_android_runtime.h"
#include "sudoku_bootstrap_runtime.h"
#include "sudoku_android_entry.h"

#include <android/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef SUDOKU_USE_GTK4
#include <gtk/gtk.h>
#include <math.h>
#endif

#define TAG  "SudokuApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

enum {
    MJ_GATE_MISSING_ACTIVITY   = 1u << 0,
    MJ_GATE_MISSING_RESUME     = 1u << 1,
    MJ_GATE_MISSING_INPUT      = 1u << 2,
    MJ_GATE_MISSING_SURFACE    = 1u << 3,
    MJ_GATE_MISSING_SIZE       = 1u << 4,
    MJ_GATE_STALE_SURFACE_CFG  = 1u << 5,
};

typedef struct {
    bool         activity_attached;
    bool         lifecycle_resumed;
    bool         surface_available;
    bool         input_ready;
    bool         gtk_started;
    bool         gtk_init_failed;
    bool         gtk_backend_hint_logged;
    int          surface_generation;
    int          configured_surface_generation;
    int          width;
    int          height;
    unsigned int last_gate_mask;
#ifdef SUDOKU_USE_GTK4
    GMainLoop   *main_loop;
#endif
} MJBootstrapState;

static MJBootstrapState s_state = {
    .activity_attached             = false,
    .lifecycle_resumed             = false,
    .surface_available             = false,
    .input_ready                   = false,
    .gtk_started                   = false,
    .gtk_init_failed               = false,
    .gtk_backend_hint_logged       = false,
    .surface_generation            = 0,
    .configured_surface_generation = 0,
    .width                         = 0,
    .height                        = 0,
    .last_gate_mask                = ~0u,
#ifdef SUDOKU_USE_GTK4
    .main_loop = NULL,
#endif
};

#ifdef SUDOKU_USE_GTK4

/* Gate-check bit logic (mirrors gdk_android_gtk_bootstrap.c) */
static void mj_check_gates(void)
{
    unsigned int cur_gate_mask = 0;

    if (!s_state.activity_attached)
        cur_gate_mask |= MJ_GATE_MISSING_ACTIVITY;
    if (!s_state.lifecycle_resumed)
        cur_gate_mask |= MJ_GATE_MISSING_RESUME;
    if (!s_state.input_ready)
        cur_gate_mask |= MJ_GATE_MISSING_INPUT;
    if (!s_state.surface_available)
        cur_gate_mask |= MJ_GATE_MISSING_SURFACE;
    if (s_state.width == 0 || s_state.height == 0)
        cur_gate_mask |= MJ_GATE_MISSING_SIZE;
    if (s_state.surface_generation != s_state.configured_surface_generation)
        cur_gate_mask |= MJ_GATE_STALE_SURFACE_CFG;

    if (cur_gate_mask != s_state.last_gate_mask) {
        LOGI("gtk_bootstrap:start gate blocked via %s mask=0x%x activity=%d resumed=%d input=%d surface=%d size=%dx%d gen=%d configured_gen=%d",
             cur_gate_mask & MJ_GATE_MISSING_ACTIVITY   ? "LIFECYCLE_CREATE"  :
             cur_gate_mask & MJ_GATE_MISSING_RESUME     ? "ACTIVITY_ATTACHED" :
             cur_gate_mask & MJ_GATE_MISSING_INPUT      ? "INPUT_INIT"        :
             cur_gate_mask & MJ_GATE_MISSING_SURFACE    ? "SURFACE_READY"     :
             cur_gate_mask & MJ_GATE_MISSING_SIZE       ? "SIZE_ALLOC"        :
             cur_gate_mask & MJ_GATE_STALE_SURFACE_CFG  ? "SURFACE_CONFIG"    : "NONE",
             cur_gate_mask,
             s_state.activity_attached, s_state.lifecycle_resumed, s_state.input_ready,
             s_state.surface_available, s_state.width, s_state.height,
             s_state.surface_generation, s_state.configured_surface_generation);
        s_state.last_gate_mask = cur_gate_mask;
    }

    if (cur_gate_mask == 0 && !s_state.gtk_started && !s_state.gtk_init_failed) {
        s_state.gtk_started = true;
        LOGI("gtk_bootstrap:start requested (surface=%p)", (void*)0);
        
        /* Initialize game entry point */
        sudoku_android_entry_init();
        
        /* Start a new game (Phase 2: receive layout/seed from Android Activity) */
        gboolean game_ok = sudoku_android_entry_new_game(NULL, 0);
        if (!game_ok) {
            LOGE("gtk_bootstrap:failed to start sudoku game");
            s_state.gtk_init_failed = true;
        } else {
            LOGI("gtk_bootstrap:sudoku game started");
        }
    }
}

#endif /* SUDOKU_USE_GTK4 */

/* ──────────────────────────────────────────────────────────────────────
 * Public interface required by gdk_android_runtime.c
 * ────────────────────────────────────────────────────────────────────── */

GdkSurface *gdk_android_gtk_bootstrap_setup_surface(
    JNIEnv *env, jclass activity_class, jintArray jni_display_size)
{
#ifdef SUDOKU_USE_GTK4
    /* Placeholder: stub returns NULL to defer full surface setup to Phase 2 */
    LOGI("gtk_bootstrap:setup_surface (stub implementation for Phase 2)");
    return NULL;
#else
    (void)env;
    (void)activity_class;
    (void)jni_display_size;
    return NULL;
#endif
}

/* ── Public interface (called by gdk_android_runtime.c) ────────────────── */

void
gdk_android_gtk_bootstrap_handle_event(GdkAndroidEventType type, int arg0, int arg1)
{
    const char *reason = "UNKNOWN";

    switch (type) {
    case GDK_ANDROID_EVENT_LIFECYCLE_CREATE:
        reason = "LIFECYCLE_CREATE";
        s_state.gtk_started  = false;
        s_state.gtk_init_failed = false;
        s_state.width        = 0;
        s_state.height       = 0;
        s_state.configured_surface_generation = 0;
        break;

    case GDK_ANDROID_EVENT_PROCESS_RESTORE_HINT:
        reason = "PROCESS_RESTORE_HINT";
        LOGI("gtk_bootstrap:process_restore_hint flags=0x%x", arg0);
        break;

    case GDK_ANDROID_EVENT_LIFECYCLE_RESUME:
        reason = "LIFECYCLE_RESUME";
        s_state.lifecycle_resumed = true;
        break;

    case GDK_ANDROID_EVENT_LIFECYCLE_PAUSE:
        reason = "LIFECYCLE_PAUSE";
        s_state.lifecycle_resumed = false;
        break;

    case GDK_ANDROID_EVENT_LIFECYCLE_DESTROY:
        reason = "LIFECYCLE_DESTROY";
        s_state.lifecycle_resumed  = false;
        s_state.surface_available  = false;
        s_state.input_ready        = false;
        s_state.gtk_started        = false;
        s_state.gtk_init_failed    = false;
        s_state.width              = 0;
        s_state.height             = 0;
        s_state.surface_generation = 0;
        s_state.configured_surface_generation = 0;
        break;

    case GDK_ANDROID_EVENT_ACTIVITY_ATTACHED:
        reason = "ACTIVITY_ATTACHED";
        s_state.activity_attached = true;
        LOGI("gtk_bootstrap:activity_attached");
        break;

    case GDK_ANDROID_EVENT_ACTIVITY_DETACHED:
        reason = "ACTIVITY_DETACHED";
        s_state.activity_attached = false;
        LOGI("gtk_bootstrap:activity_detached");
        break;

    case GDK_ANDROID_EVENT_SURFACE_CREATED:
        reason = "SURFACE_CREATED";
        s_state.surface_available = true;
        if (arg0 > 0) {
            s_state.surface_generation = arg0;
        }
        s_state.configured_surface_generation = 0;
        s_state.width  = 0;
        s_state.height = 0;
        LOGI("gtk_bootstrap:surface_created gen=%d", s_state.surface_generation);
        break;

    case GDK_ANDROID_EVENT_SURFACE_CHANGED:
        reason = "SURFACE_CHANGED";
        s_state.width  = arg0;
        s_state.height = arg1;
        if (arg0 > 0 && arg1 > 0 && s_state.surface_available) {
            s_state.configured_surface_generation = s_state.surface_generation;
            LOGI("gtk_bootstrap:surface_changed %dx%d gen=%d",
                 arg0, arg1, s_state.surface_generation);
        }
        break;

    case GDK_ANDROID_EVENT_SURFACE_DESTROYED:
        reason = "SURFACE_DESTROYED";
        s_state.surface_available             = false;
        s_state.configured_surface_generation = 0;
        s_state.width                         = 0;
        s_state.height                        = 0;
        if (s_state.gtk_started) {
            LOGI("gtk_bootstrap:surface_destroyed gen=%d, restart required", arg0);
            s_state.gtk_started = false;
        } else {
            LOGI("gtk_bootstrap:surface_destroyed gen=%d", arg0);
        }
        break;

    case GDK_ANDROID_EVENT_INPUT_INIT:
        reason = "INPUT_INIT";
        s_state.input_ready = true;
        LOGI("gtk_bootstrap:input initialized");
        break;

    case GDK_ANDROID_EVENT_INPUT_MOTION:
        return;

    case GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_UP:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL: {
        GdkAndroidTouchEvent touch_event;

        if (!gdk_android_input_pop_touch_event(&touch_event)) {
            return;
        }

        if (!touch_event.lifecycle_valid || touch_event.phase != GDK_ANDROID_TOUCH_PHASE_UP) {
            return;
        }

        int tile_index = sudoku_android_entry_pick_tile_from_surface(
            touch_event.x,
            touch_event.y,
            s_state.width,
            s_state.height);
        if (tile_index < 0) {
            return;
        }

        (void)sudoku_android_entry_select_tile(tile_index);
        return;
    }

    case GDK_ANDROID_EVENT_IME_SHOW:
    case GDK_ANDROID_EVENT_IME_HIDE:
    case GDK_ANDROID_EVENT_IME_COMMIT:
        /* IME events are handled by the GDK Android input layer. */
        return;

    default:
        LOGI("gtk_bootstrap:unhandled event type=%d arg0=%d arg1=%d", type, arg0, arg1);
        return;
    }

    /* Check gates after handling non-return events */
    mj_check_gates();
}

/* ── Test-infra stubs (no-op for sudoku; used by app-module smoke only) */

void
gdk_android_gtk_bootstrap_seed_test_entry_state(const char *text)
{
    (void)text;
}

void
gdk_android_gtk_bootstrap_seed_test_button_count(unsigned int count)
{
    (void)count;
}

void
gdk_android_gtk_bootstrap_seed_test_toggle_active(bool active)
{
    (void)active;
}
