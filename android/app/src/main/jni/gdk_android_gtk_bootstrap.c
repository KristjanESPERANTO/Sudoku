/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gdk_android_gtk_bootstrap.h"
#include "gdk_android_input.h"
#include "gdk_android_runtime.h"

#include <android/log.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef GDK_ANDROID_USE_GTK4
#include <gtk/gtk.h>
#endif

#define TAG "gnome-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define IME_LOG_BUFFER_MAX 256
#define TEST_UI_STATE_PATH "/data/user/0/org.gnome.android/files/gtk-test-ui-state.ini"
#define TEST_ENTRY_STATE_PATH "/data/user/0/org.gnome.android/files/gtk-test-entry-state.txt"
#define TEST_BUTTON_COUNT_STATE_PATH "/data/user/0/org.gnome.android/files/gtk-test-button-count.txt"
#define TEST_UI_STATE_SCHEMA_VERSION 1
#define TEST_UI_STATE_GROUP "gtk_test"
#define TEST_UI_STATE_KEY_ENTRY_TEXT "entry_text"
#define TEST_UI_STATE_KEY_BUTTON_COUNT "button_count"
#define TEST_UI_STATE_KEY_TOGGLE_ACTIVE "toggle_active"
#define TEST_UI_STATE_KEY_SCHEMA_VERSION "schema_version"
#define TEST_UI_STATE_KEY_SAVE_SEQUENCE "save_sequence"
#define TEST_UI_STATE_KEY_SAVED_AT_MS "saved_at_ms"
#define APP_SESSION_STATE_GROUP "app_session_v1"
#define APP_SESSION_SCHEMA_VERSION 1
#define APP_SESSION_KEY_SCHEMA_VERSION "schema_version"
#define APP_SESSION_KEY_LAST_RESTORE_FLAGS "last_restore_flags"
#define APP_SESSION_KEY_RESTORE_HINT_COUNT "restore_hint_count"
#define APP_SESSION_KEY_LAST_RESTORE_HINT_MS "last_restore_hint_ms"
#define APP_SESSION_KEY_LAST_SURFACE_WIDTH "last_surface_width"
#define APP_SESSION_KEY_LAST_SURFACE_HEIGHT "last_surface_height"
#define APP_SESSION_KEY_LAST_SURFACE_GENERATION "last_surface_generation"
#define APP_SESSION_KEY_LAST_SURFACE_UPDATED_MS "last_surface_updated_ms"
#define APP_SESSION_KEY_LAST_LIFECYCLE_EVENT "last_lifecycle_event"
#define APP_SESSION_KEY_LAST_LIFECYCLE_EVENT_MS "last_lifecycle_event_ms"
#define APP_SESSION_KEY_LAST_GATE_MASK "last_gate_mask"
#define APP_SESSION_KEY_RESTORE_COUNT "restore_count"

enum {
    APP_SESSION_LIFECYCLE_UNKNOWN = 0,
    APP_SESSION_LIFECYCLE_CREATE = 1,
    APP_SESSION_LIFECYCLE_RESUME = 2,
    APP_SESSION_LIFECYCLE_PAUSE = 3,
    APP_SESSION_LIFECYCLE_DESTROY = 4,
    APP_SESSION_LIFECYCLE_ACTIVITY_ATTACHED = 5,
    APP_SESSION_LIFECYCLE_ACTIVITY_DETACHED = 6,
};

typedef struct {
    bool activity_attached;
    bool lifecycle_resumed;
    bool surface_available;
    bool input_ready;
    bool restore_entry_state_requested;
    bool gtk_started;
    bool gtk_initialized;
    bool gtk_init_failed;
    bool gtk_backend_hint_logged;
    int surface_generation;
    int configured_surface_generation;
    int width;
    int height;
    unsigned int last_gate_mask;
#ifdef GDK_ANDROID_USE_GTK4
    GMainLoop *main_loop;
    GtkWidget *test_button;
    GtkWidget *test_toggle;
    unsigned int test_button_click_count;
    bool test_toggle_active;
#endif
} GtkBootstrapState;

static GtkBootstrapState s_state = {
    .activity_attached = false,
    .lifecycle_resumed = false,
    .surface_available = false,
    .input_ready = false,
    .restore_entry_state_requested = false,
    .gtk_started = false,
    .gtk_initialized = false,
    .gtk_init_failed = false,
    .gtk_backend_hint_logged = false,
    .surface_generation = 0,
    .configured_surface_generation = 0,
    .width = 0,
    .height = 0,
    .last_gate_mask = 0,
#ifdef GDK_ANDROID_USE_GTK4
    .main_loop = NULL,
    .test_button = NULL,
    .test_toggle = NULL,
    .test_button_click_count = 0,
    .test_toggle_active = false,
#endif
};

enum {
    GTK_GATE_MISSING_ACTIVITY = 1u << 0,
    GTK_GATE_MISSING_RESUME = 1u << 1,
    GTK_GATE_MISSING_INPUT = 1u << 2,
    GTK_GATE_MISSING_SURFACE = 1u << 3,
    GTK_GATE_MISSING_SIZE = 1u << 4,
    GTK_GATE_STALE_SURFACE_CONFIG = 1u << 5,
};

#ifdef GDK_ANDROID_USE_GTK4
typedef struct {
    char *entry_text;
    unsigned int button_count;
    bool toggle_active;
    unsigned int schema_version;
    guint64 save_sequence;
    gint64 saved_at_ms;
    unsigned int app_session_schema_version;
    unsigned int app_session_last_restore_flags;
    guint64 app_session_restore_hint_count;
    gint64 app_session_last_restore_hint_ms;
    int app_session_last_surface_width;
    int app_session_last_surface_height;
    int app_session_last_surface_generation;
    gint64 app_session_last_surface_updated_ms;
    int app_session_last_lifecycle_event;
    gint64 app_session_last_lifecycle_event_ms;
    unsigned int app_session_last_gate_mask;
    guint64 app_session_restore_count;
} GtkTestUiState;

static void
gtk_test_ui_state_clear(GtkTestUiState *state)
{
    if (state == NULL) {
        return;
    }

    g_free(state->entry_text);
    state->entry_text = NULL;
    state->button_count = 0;
    state->toggle_active = false;
    state->schema_version = 0;
    state->save_sequence = 0;
    state->saved_at_ms = 0;
    state->app_session_schema_version = 0;
    state->app_session_last_restore_flags = 0;
    state->app_session_restore_hint_count = 0;
    state->app_session_last_restore_hint_ms = 0;
    state->app_session_last_surface_width = 0;
    state->app_session_last_surface_height = 0;
    state->app_session_last_surface_generation = 0;
    state->app_session_last_surface_updated_ms = 0;
    state->app_session_last_lifecycle_event = APP_SESSION_LIFECYCLE_UNKNOWN;
    state->app_session_last_lifecycle_event_ms = 0;
    state->app_session_last_gate_mask = 0;
    state->app_session_restore_count = 0;
}

static char *
test_ui_state_path(void)
{
    return g_strdup(TEST_UI_STATE_PATH);
}

static char *
test_entry_state_path(void)
{
    return g_strdup(TEST_ENTRY_STATE_PATH);
}

static char *
test_button_count_state_path(void)
{
    return g_strdup(TEST_BUTTON_COUNT_STATE_PATH);
}

static bool
parse_uint_from_text(const char *text, unsigned int *out_count)
{
    char *trimmed;
    char *endptr = NULL;
    guint64 parsed;

    if (text == NULL || out_count == NULL) {
        return false;
    }

    trimmed = g_strdup(text);
    if (trimmed == NULL) {
        return false;
    }

    g_strstrip(trimmed);
    if (*trimmed == '\0') {
        g_free(trimmed);
        return false;
    }

    parsed = g_ascii_strtoull(trimmed, &endptr, 10);
    if (endptr == trimmed || (endptr != NULL && *endptr != '\0') || parsed > G_MAXUINT) {
        g_free(trimmed);
        return false;
    }

    *out_count = (unsigned int)parsed;
    g_free(trimmed);
    return true;
}

static bool
load_test_ui_state_bundle(GtkTestUiState *out_state)
{
    char *path;
    char *contents = NULL;
    GKeyFile *key_file;
    bool loaded = false;

    if (out_state == NULL) {
        return false;
    }

    out_state->entry_text = NULL;
    out_state->button_count = 0;
    out_state->toggle_active = false;
    out_state->schema_version = 0;
    out_state->save_sequence = 0;
    out_state->saved_at_ms = 0;
    out_state->app_session_schema_version = 0;
    out_state->app_session_last_restore_flags = 0;
    out_state->app_session_restore_hint_count = 0;
    out_state->app_session_last_restore_hint_ms = 0;
    out_state->app_session_last_surface_width = 0;
    out_state->app_session_last_surface_height = 0;
    out_state->app_session_last_surface_generation = 0;
    out_state->app_session_last_surface_updated_ms = 0;
    out_state->app_session_last_lifecycle_event = APP_SESSION_LIFECYCLE_UNKNOWN;
    out_state->app_session_last_lifecycle_event_ms = 0;
    out_state->app_session_last_gate_mask = 0;
    out_state->app_session_restore_count = 0;

    path = test_ui_state_path();
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_free(path);
        return false;
    }
    g_free(path);

    key_file = g_key_file_new();
    if (g_key_file_load_from_data(key_file, contents, -1, G_KEY_FILE_NONE, NULL)) {
        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_ENTRY_TEXT, NULL)) {
            out_state->entry_text = g_key_file_get_string(key_file,
                                                           TEST_UI_STATE_GROUP,
                                                           TEST_UI_STATE_KEY_ENTRY_TEXT,
                                                           NULL);
        }

        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_BUTTON_COUNT, NULL)) {
            gint64 count = g_key_file_get_int64(key_file,
                                                TEST_UI_STATE_GROUP,
                                                TEST_UI_STATE_KEY_BUTTON_COUNT,
                                                NULL);
            if (count >= 0 && count <= G_MAXUINT) {
                out_state->button_count = (unsigned int)count;
            }
        }

        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_TOGGLE_ACTIVE, NULL)) {
            out_state->toggle_active = g_key_file_get_boolean(key_file,
                                                              TEST_UI_STATE_GROUP,
                                                              TEST_UI_STATE_KEY_TOGGLE_ACTIVE,
                                                              NULL);
        }

        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_SCHEMA_VERSION, NULL)) {
            gint64 schema = g_key_file_get_int64(key_file,
                                                 TEST_UI_STATE_GROUP,
                                                 TEST_UI_STATE_KEY_SCHEMA_VERSION,
                                                 NULL);
            if (schema >= 0 && schema <= G_MAXUINT) {
                out_state->schema_version = (unsigned int)schema;
            }
        }

        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_SAVE_SEQUENCE, NULL)) {
            gint64 seq = g_key_file_get_int64(key_file,
                                              TEST_UI_STATE_GROUP,
                                              TEST_UI_STATE_KEY_SAVE_SEQUENCE,
                                              NULL);
            if (seq >= 0) {
                out_state->save_sequence = (guint64)seq;
            }
        }

        if (g_key_file_has_key(key_file, TEST_UI_STATE_GROUP, TEST_UI_STATE_KEY_SAVED_AT_MS, NULL)) {
            out_state->saved_at_ms = g_key_file_get_int64(key_file,
                                                          TEST_UI_STATE_GROUP,
                                                          TEST_UI_STATE_KEY_SAVED_AT_MS,
                                                          NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_SCHEMA_VERSION, NULL)) {
            gint64 schema = g_key_file_get_int64(key_file,
                                                 APP_SESSION_STATE_GROUP,
                                                 APP_SESSION_KEY_SCHEMA_VERSION,
                                                 NULL);
            if (schema >= 0 && schema <= G_MAXUINT) {
                out_state->app_session_schema_version = (unsigned int)schema;
            }
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_RESTORE_FLAGS, NULL)) {
            gint64 flags = g_key_file_get_int64(key_file,
                                                APP_SESSION_STATE_GROUP,
                                                APP_SESSION_KEY_LAST_RESTORE_FLAGS,
                                                NULL);
            if (flags >= 0 && flags <= G_MAXUINT) {
                out_state->app_session_last_restore_flags = (unsigned int)flags;
            }
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_RESTORE_HINT_COUNT, NULL)) {
            gint64 count = g_key_file_get_int64(key_file,
                                                APP_SESSION_STATE_GROUP,
                                                APP_SESSION_KEY_RESTORE_HINT_COUNT,
                                                NULL);
            if (count >= 0) {
                out_state->app_session_restore_hint_count = (guint64)count;
            }
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_RESTORE_HINT_MS, NULL)) {
            out_state->app_session_last_restore_hint_ms = g_key_file_get_int64(key_file,
                                                                                APP_SESSION_STATE_GROUP,
                                                                                APP_SESSION_KEY_LAST_RESTORE_HINT_MS,
                                                                                NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_SURFACE_WIDTH, NULL)) {
            out_state->app_session_last_surface_width = (int)g_key_file_get_int64(key_file,
                                                                                   APP_SESSION_STATE_GROUP,
                                                                                   APP_SESSION_KEY_LAST_SURFACE_WIDTH,
                                                                                   NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_SURFACE_HEIGHT, NULL)) {
            out_state->app_session_last_surface_height = (int)g_key_file_get_int64(key_file,
                                                                                    APP_SESSION_STATE_GROUP,
                                                                                    APP_SESSION_KEY_LAST_SURFACE_HEIGHT,
                                                                                    NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_SURFACE_GENERATION, NULL)) {
            out_state->app_session_last_surface_generation = (int)g_key_file_get_int64(key_file,
                                                                                        APP_SESSION_STATE_GROUP,
                                                                                        APP_SESSION_KEY_LAST_SURFACE_GENERATION,
                                                                                        NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_SURFACE_UPDATED_MS, NULL)) {
            out_state->app_session_last_surface_updated_ms = g_key_file_get_int64(key_file,
                                                                                   APP_SESSION_STATE_GROUP,
                                                                                   APP_SESSION_KEY_LAST_SURFACE_UPDATED_MS,
                                                                                   NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_LIFECYCLE_EVENT, NULL)) {
            out_state->app_session_last_lifecycle_event = (int)g_key_file_get_int64(key_file,
                                                                                     APP_SESSION_STATE_GROUP,
                                                                                     APP_SESSION_KEY_LAST_LIFECYCLE_EVENT,
                                                                                     NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_LIFECYCLE_EVENT_MS, NULL)) {
            out_state->app_session_last_lifecycle_event_ms = g_key_file_get_int64(key_file,
                                                                                   APP_SESSION_STATE_GROUP,
                                                                                   APP_SESSION_KEY_LAST_LIFECYCLE_EVENT_MS,
                                                                                   NULL);
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_LAST_GATE_MASK, NULL)) {
            gint64 gate_mask = g_key_file_get_int64(key_file,
                                                    APP_SESSION_STATE_GROUP,
                                                    APP_SESSION_KEY_LAST_GATE_MASK,
                                                    NULL);
            if (gate_mask >= 0 && gate_mask <= G_MAXUINT) {
                out_state->app_session_last_gate_mask = (unsigned int)gate_mask;
            }
        }

        if (g_key_file_has_key(key_file, APP_SESSION_STATE_GROUP, APP_SESSION_KEY_RESTORE_COUNT, NULL)) {
            gint64 restore_count = g_key_file_get_int64(key_file,
                                                       APP_SESSION_STATE_GROUP,
                                                       APP_SESSION_KEY_RESTORE_COUNT,
                                                       NULL);
            if (restore_count >= 0) {
                out_state->app_session_restore_count = (guint64)restore_count;
            }
        }

        loaded = true;
    }

    g_key_file_unref(key_file);
    g_free(contents);
    return loaded;
}

static bool
save_test_ui_state_bundle(GtkTestUiState *state)
{
    char *path;
    GKeyFile *key_file;
    gsize length = 0;
    char *serialized;
    bool success = false;

    if (state == NULL) {
        return false;
    }

    state->schema_version = TEST_UI_STATE_SCHEMA_VERSION;
    state->save_sequence++;
    state->saved_at_ms = (gint64)(g_get_real_time() / 1000);

    key_file = g_key_file_new();
    g_key_file_set_string(key_file,
                          TEST_UI_STATE_GROUP,
                          TEST_UI_STATE_KEY_ENTRY_TEXT,
                          state->entry_text != NULL ? state->entry_text : "");
    g_key_file_set_int64(key_file,
                         TEST_UI_STATE_GROUP,
                         TEST_UI_STATE_KEY_BUTTON_COUNT,
                         (gint64)state->button_count);
    g_key_file_set_boolean(key_file,
                           TEST_UI_STATE_GROUP,
                           TEST_UI_STATE_KEY_TOGGLE_ACTIVE,
                           state->toggle_active);
    g_key_file_set_int64(key_file,
                         TEST_UI_STATE_GROUP,
                         TEST_UI_STATE_KEY_SCHEMA_VERSION,
                         (gint64)state->schema_version);
    g_key_file_set_int64(key_file,
                         TEST_UI_STATE_GROUP,
                         TEST_UI_STATE_KEY_SAVE_SEQUENCE,
                         (gint64)state->save_sequence);
    g_key_file_set_int64(key_file,
                         TEST_UI_STATE_GROUP,
                         TEST_UI_STATE_KEY_SAVED_AT_MS,
                         state->saved_at_ms);

    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_SCHEMA_VERSION,
                         (gint64)state->app_session_schema_version);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_RESTORE_FLAGS,
                         (gint64)state->app_session_last_restore_flags);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_RESTORE_HINT_COUNT,
                         (gint64)state->app_session_restore_hint_count);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_RESTORE_HINT_MS,
                         state->app_session_last_restore_hint_ms);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_SURFACE_WIDTH,
                         (gint64)state->app_session_last_surface_width);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_SURFACE_HEIGHT,
                         (gint64)state->app_session_last_surface_height);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_SURFACE_GENERATION,
                         (gint64)state->app_session_last_surface_generation);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_SURFACE_UPDATED_MS,
                         state->app_session_last_surface_updated_ms);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_LIFECYCLE_EVENT,
                         (gint64)state->app_session_last_lifecycle_event);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_LIFECYCLE_EVENT_MS,
                         state->app_session_last_lifecycle_event_ms);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_LAST_GATE_MASK,
                         (gint64)state->app_session_last_gate_mask);
    g_key_file_set_int64(key_file,
                         APP_SESSION_STATE_GROUP,
                         APP_SESSION_KEY_RESTORE_COUNT,
                         (gint64)state->app_session_restore_count);

    serialized = g_key_file_to_data(key_file, &length, NULL);
    path = test_ui_state_path();
    if (serialized != NULL) {
        success = g_file_set_contents(path, serialized, (gssize)length, NULL);
    }

    g_free(path);
    g_free(serialized);
    g_key_file_unref(key_file);
    return success;
}

static char *
load_legacy_test_entry_state(void)
{
    char *path;
    char *contents = NULL;

    path = test_entry_state_path();
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_free(path);
        return NULL;
    }

    g_free(path);
    return contents;
}

static bool
load_legacy_test_button_count_state(unsigned int *out_count)
{
    char *path;
    char *contents = NULL;
    bool ok;

    if (out_count == NULL) {
        return false;
    }

    path = test_button_count_state_path();
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_free(path);
        return false;
    }
    g_free(path);

    ok = parse_uint_from_text(contents, out_count);
    g_free(contents);
    return ok;
}

static void
save_test_entry_state(const char *text)
{
    GtkTestUiState state = {0};

    if (!load_test_ui_state_bundle(&state)) {
        load_legacy_test_button_count_state(&state.button_count);
    }

    g_free(state.entry_text);
    state.entry_text = g_strdup(text ? text : "");

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist test entry state");
    }

    gtk_test_ui_state_clear(&state);
}

static void
save_test_toggle_state(bool active)
{
    GtkTestUiState state = {0};

    if (!load_test_ui_state_bundle(&state)) {
        state.entry_text = load_legacy_test_entry_state();
        load_legacy_test_button_count_state(&state.button_count);
    }

    state.toggle_active = active;

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist test toggle state");
    }

    gtk_test_ui_state_clear(&state);
}

void
gdk_android_gtk_bootstrap_seed_test_entry_state(const char *text)
{
    save_test_entry_state(text);
    LOGI("gtk_bootstrap:seeded test entry state='%s'", text ? text : "");
}

static void
save_test_button_count_state(unsigned int count)
{
    GtkTestUiState state = {0};

    if (!load_test_ui_state_bundle(&state)) {
        state.entry_text = load_legacy_test_entry_state();
    }

    state.button_count = count;

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist test button count state");
    }

    gtk_test_ui_state_clear(&state);
}

void
gdk_android_gtk_bootstrap_seed_test_button_count(unsigned int count)
{
    save_test_button_count_state(count);
    LOGI("gtk_bootstrap:seeded test button count=%u", count);
}

void
gdk_android_gtk_bootstrap_seed_test_toggle_active(bool active)
{
    save_test_toggle_state(active);
    LOGI("gtk_bootstrap:seeded test toggle active=%d", active ? 1 : 0);
}

static char *
load_test_entry_state(void)
{
    GtkTestUiState state = {0};

    if (load_test_ui_state_bundle(&state) && state.entry_text != NULL) {
        return state.entry_text;
    }

    gtk_test_ui_state_clear(&state);
    return load_legacy_test_entry_state();
}

static bool
load_test_button_count_state(unsigned int *out_count)
{
    GtkTestUiState state = {0};

    if (out_count == NULL) {
        return false;
    }

    if (load_test_ui_state_bundle(&state)) {
        *out_count = state.button_count;
        gtk_test_ui_state_clear(&state);
        return true;
    }

    return load_legacy_test_button_count_state(out_count);
}

static void
record_restore_hint_session_state(int restore_flags)
{
    GtkTestUiState state = {0};

    load_test_ui_state_bundle(&state);
    state.app_session_schema_version = APP_SESSION_SCHEMA_VERSION;
    state.app_session_last_restore_flags = restore_flags >= 0 ? (unsigned int)restore_flags : 0;
    state.app_session_restore_hint_count++;
    state.app_session_last_restore_hint_ms = (gint64)(g_get_real_time() / 1000);

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist app session state");
    } else {
        LOGI("gtk_bootstrap:updated app session v1 flags=0x%x restore_hint_count=%" G_GUINT64_FORMAT " last_restore_hint_ms=%" G_GINT64_FORMAT,
             state.app_session_last_restore_flags,
             state.app_session_restore_hint_count,
             state.app_session_last_restore_hint_ms);
    }

    gtk_test_ui_state_clear(&state);
}

static void
record_surface_session_state(int width, int height, int generation)
{
    GtkTestUiState state = {0};

    load_test_ui_state_bundle(&state);
    state.app_session_schema_version = APP_SESSION_SCHEMA_VERSION;
    state.app_session_last_surface_width = width;
    state.app_session_last_surface_height = height;
    state.app_session_last_surface_generation = generation;
    state.app_session_last_surface_updated_ms = (gint64)(g_get_real_time() / 1000);

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist app surface session state");
    } else {
        LOGI("gtk_bootstrap:updated app surface session width=%d height=%d gen=%d updated_ms=%" G_GINT64_FORMAT,
             state.app_session_last_surface_width,
             state.app_session_last_surface_height,
             state.app_session_last_surface_generation,
             state.app_session_last_surface_updated_ms);
    }

    gtk_test_ui_state_clear(&state);
}

static void
record_lifecycle_session_state(int lifecycle_event)
{
    GtkTestUiState state = {0};

    load_test_ui_state_bundle(&state);
    state.app_session_schema_version = APP_SESSION_SCHEMA_VERSION;
    state.app_session_last_lifecycle_event = lifecycle_event;
    state.app_session_last_lifecycle_event_ms = (gint64)(g_get_real_time() / 1000);

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist app lifecycle session state");
    } else {
        LOGI("gtk_bootstrap:updated app lifecycle session event=%d updated_ms=%" G_GINT64_FORMAT,
             state.app_session_last_lifecycle_event,
             state.app_session_last_lifecycle_event_ms);
    }

    gtk_test_ui_state_clear(&state);
}

static void
record_gate_mask_session_state(unsigned int gate_mask)
{
    GtkTestUiState state = {0};

    load_test_ui_state_bundle(&state);
    state.app_session_schema_version = APP_SESSION_SCHEMA_VERSION;
    state.app_session_last_gate_mask = gate_mask;

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist app gate session state");
    } else {
        LOGI("gtk_bootstrap:updated app gate session mask=0x%x", state.app_session_last_gate_mask);
    }

    gtk_test_ui_state_clear(&state);
}

static void
increment_restore_count_session_state(void)
{
    GtkTestUiState state = {0};

    load_test_ui_state_bundle(&state);
    state.app_session_schema_version = APP_SESSION_SCHEMA_VERSION;
    state.app_session_restore_count++;

    if (!save_test_ui_state_bundle(&state)) {
        LOGI("gtk_bootstrap:failed to persist restore count");
    } else {
        LOGI("gtk_bootstrap:incremented app restore count=%" G_GUINT64_FORMAT, state.app_session_restore_count);
    }

    gtk_test_ui_state_clear(&state);
}

static void
update_test_button_label(void)
{
    if (s_state.test_button == NULL) {
        return;
    }

    if (s_state.test_button_click_count == 0) {
        gtk_button_set_label(GTK_BUTTON(s_state.test_button), "GTK Button Smoke Test");
    } else {
        char *label_text = g_strdup_printf("GTK Button Smoke Test (%u)", s_state.test_button_click_count);
        gtk_button_set_label(GTK_BUTTON(s_state.test_button), label_text);
        g_free(label_text);
    }
}

static void
apply_test_toggle_state(void)
{
    if (s_state.test_toggle != NULL) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s_state.test_toggle), s_state.test_toggle_active);
    }
}

static void on_test_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    (void)button;

    s_state.test_button_click_count++;
    save_test_button_count_state(s_state.test_button_click_count);
    update_test_button_label();
    LOGI("gtk_bootstrap:test_button clicked count=%u", s_state.test_button_click_count);
}

static void on_test_toggle_toggled(GtkCheckButton *check_button, gpointer user_data)
{
    (void)user_data;

    s_state.test_toggle_active = gtk_check_button_get_active(check_button);
    save_test_toggle_state(s_state.test_toggle_active);
    LOGI("gtk_bootstrap:test_toggle changed active=%d", s_state.test_toggle_active ? 1 : 0);
}

static void on_test_entry_changed(GtkEditable *editable, gpointer user_data)
{
    (void)user_data;
    const char *text = gtk_editable_get_text(editable);

    save_test_entry_state(text);
    LOGI("gtk_bootstrap:entry changed text='%s'", text ? text : "(null)");
}

static bool start_gtk_test_window(void)
{
    GtkWidget *window;
    GtkWidget *content;
    GtkWidget *label;
    GtkWidget *button;
    GtkWidget *toggle;
    GtkWidget *entry;

    if (g_getenv("GSK_RENDERER") == NULL) {
        g_setenv("GSK_RENDERER", "ngl", TRUE);
        LOGI("gtk_bootstrap:forcing GSK_RENDERER=ngl for Android smoke test");
    }

    if (!s_state.gtk_initialized) {
        /* fontconfig 2.17.1+ finds /system/fonts via compiled-in Android
         * defaults — no manual fonts.conf setup needed (L1 resolved upstream). */
        if (!gtk_init_check()) {
            LOGI("gtk_bootstrap:gtk_init_check failed (backend unavailable)");
            if (!s_state.gtk_backend_hint_logged) {
                LOGI("gtk_bootstrap:hint Android backend is now built, but runtime bridge/context is still not initialized for this launcher path");
                s_state.gtk_backend_hint_logged = true;
            }
            s_state.gtk_init_failed = true;
            return false;
        }
        s_state.gtk_initialized = true;
    }

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "GNOME Android PoC");
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 640);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);

    label = gtk_label_new("GTK4 Android PoC: Tag 3 — Touch + IME");
    gtk_box_append(GTK_BOX(content), label);

    button = gtk_button_new_with_label("GTK Button Smoke Test");
    s_state.test_button = button;
    update_test_button_label();
    g_signal_connect(button, "clicked", G_CALLBACK(on_test_button_clicked), NULL);
    gtk_box_append(GTK_BOX(content), button);

    toggle = gtk_check_button_new_with_label("GTK Toggle Smoke Test");
    s_state.test_toggle = toggle;
    apply_test_toggle_state();
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_test_toggle_toggled), NULL);
    gtk_box_append(GTK_BOX(content), toggle);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Tap here to open IME…");
    g_signal_connect(entry, "changed", G_CALLBACK(on_test_entry_changed), NULL);

    if (s_state.restore_entry_state_requested) {
        GtkTestUiState restored_ui_state = {0};
        if (load_test_ui_state_bundle(&restored_ui_state)) {
            if (restored_ui_state.entry_text != NULL) {
                gtk_editable_set_text(GTK_EDITABLE(entry), restored_ui_state.entry_text);
                LOGI("gtk_bootstrap:restored test entry text='%s'", restored_ui_state.entry_text);
            } else {
                LOGI("gtk_bootstrap:no persisted test entry state found for restore");
            }

            s_state.test_button_click_count = restored_ui_state.button_count;
            update_test_button_label();
            LOGI("gtk_bootstrap:restored test button count=%u", restored_ui_state.button_count);

            s_state.test_toggle_active = restored_ui_state.toggle_active;
            apply_test_toggle_state();
            LOGI("gtk_bootstrap:restored test toggle active=%d", s_state.test_toggle_active ? 1 : 0);

            LOGI("gtk_bootstrap:restored ui state metadata schema=%u save_sequence=%" G_GUINT64_FORMAT " saved_at_ms=%" G_GINT64_FORMAT,
                 restored_ui_state.schema_version,
                 restored_ui_state.save_sequence,
                 restored_ui_state.saved_at_ms);
              LOGI("gtk_bootstrap:restored app session v1 schema=%u last_restore_flags=0x%x restore_hint_count=%" G_GUINT64_FORMAT " last_restore_hint_ms=%" G_GINT64_FORMAT " last_surface=%dx%d gen=%d last_surface_updated_ms=%" G_GINT64_FORMAT " last_lifecycle_event=%d last_lifecycle_event_ms=%" G_GINT64_FORMAT " last_gate_mask=0x%x",
                  restored_ui_state.app_session_schema_version,
                  restored_ui_state.app_session_last_restore_flags,
                  restored_ui_state.app_session_restore_hint_count,
                  restored_ui_state.app_session_last_restore_hint_ms,
                  restored_ui_state.app_session_last_surface_width,
                  restored_ui_state.app_session_last_surface_height,
                  restored_ui_state.app_session_last_surface_generation,
                  restored_ui_state.app_session_last_surface_updated_ms,
                  restored_ui_state.app_session_last_lifecycle_event,
                  restored_ui_state.app_session_last_lifecycle_event_ms,
                  restored_ui_state.app_session_last_gate_mask);
        } else {
            char *restored_text = load_test_entry_state();

            if (restored_text != NULL) {
                gtk_editable_set_text(GTK_EDITABLE(entry), restored_text);
                LOGI("gtk_bootstrap:restored test entry text='%s'", restored_text);
                g_free(restored_text);
            } else {
                LOGI("gtk_bootstrap:no persisted test entry state found for restore");
            }

            unsigned int restored_button_count = 0;
            if (load_test_button_count_state(&restored_button_count)) {
                s_state.test_button_click_count = restored_button_count;
                update_test_button_label();
                LOGI("gtk_bootstrap:restored test button count=%u", restored_button_count);
            } else {
                LOGI("gtk_bootstrap:no persisted test button count state found for restore");
            }

            LOGI("gtk_bootstrap:no persisted test toggle state found for restore");
            LOGI("gtk_bootstrap:no persisted app session state found for restore");
        }
        gtk_test_ui_state_clear(&restored_ui_state);

        /* Increment restore count to track successful restore cycles. */
        increment_restore_count_session_state();

        s_state.restore_entry_state_requested = false;
    }

    gtk_box_append(GTK_BOX(content), entry);

    gtk_window_set_child(GTK_WINDOW(window), content);
    gtk_window_present(GTK_WINDOW(window));

    LOGI("gtk_bootstrap:GTK test window created (label+button+entry)");

    /* Attach the Android event source so that events pushed via
     * gdk_android_runtime_push_event() are dispatched by the GLib main
     * loop while GTK's frame-clock drives rendering. */
    GSource *android_src = gdk_android_runtime_create_event_source();
    if (android_src != NULL) {
        g_source_attach(android_src, NULL);
        g_source_unref(android_src);
        LOGI("gtk_bootstrap:android event source attached");
    } else {
        LOGI("gtk_bootstrap:android event source creation failed");
    }

    /* Enter the GLib main loop — this drives GTK's frame-clock and
     * will process all GTK/GDK/GLib events (including Android events
     * delivered via the event source above) until the app is destroyed. */
    s_state.main_loop = g_main_loop_new(NULL, FALSE);
    LOGI("gtk_bootstrap:entering GLib main loop");
    g_main_loop_run(s_state.main_loop);
    LOGI("gtk_bootstrap:GLib main loop exited");
    g_main_loop_unref(s_state.main_loop);
    s_state.main_loop = NULL;

    return true;
}
#endif

static unsigned int current_gate_mask(void)
{
    unsigned int mask = 0;

    if (!s_state.activity_attached) {
        mask |= GTK_GATE_MISSING_ACTIVITY;
    }
    if (!s_state.lifecycle_resumed) {
        mask |= GTK_GATE_MISSING_RESUME;
    }
    if (!s_state.input_ready) {
        mask |= GTK_GATE_MISSING_INPUT;
    }
    if (!s_state.surface_available || s_state.surface_generation <= 0) {
        mask |= GTK_GATE_MISSING_SURFACE;
    }
    if (s_state.width <= 0 || s_state.height <= 0) {
        mask |= GTK_GATE_MISSING_SIZE;
    }
    if (s_state.surface_generation <= 0 ||
        s_state.configured_surface_generation != s_state.surface_generation) {
        mask |= GTK_GATE_STALE_SURFACE_CONFIG;
    }

    return mask;
}

static void log_gate_state_if_needed(const char *reason)
{
    unsigned int mask = current_gate_mask();

    if (mask == s_state.last_gate_mask) {
        return;
    }

    s_state.last_gate_mask = mask;
    record_gate_mask_session_state(mask);

    if (mask == 0) {
        LOGI("gtk_bootstrap:start gate satisfied via %s (surface=%dx%d gen=%d)",
             reason,
             s_state.width,
             s_state.height,
             s_state.surface_generation);
        return;
    }

    LOGI("gtk_bootstrap:start gate blocked via %s mask=0x%x activity=%d resumed=%d input=%d surface=%d size=%dx%d gen=%d configured_gen=%d",
         reason,
         mask,
         s_state.activity_attached,
         s_state.lifecycle_resumed,
         s_state.input_ready,
         s_state.surface_available,
         s_state.width,
         s_state.height,
         s_state.surface_generation,
         s_state.configured_surface_generation);
}

static void try_start_gtk(const char *reason)
{
    if (s_state.gtk_started) {
        return;
    }
    if (s_state.gtk_init_failed) {
        return;
    }

    log_gate_state_if_needed(reason);

    if (current_gate_mask() != 0) {
        return;
    }

    s_state.gtk_started = true;
    LOGI("gtk_bootstrap:start requested (surface=%dx%d)", s_state.width, s_state.height);
#ifdef GDK_ANDROID_USE_GTK4
    if (!start_gtk_test_window()) {
        s_state.gtk_started = false;
        LOGI("gtk_bootstrap:start deferred (GTK init failed)");
    }
#else
    LOGI("gtk_bootstrap:GTK4 not linked in app build yet (fallback mode)");
    LOGI("gtk_bootstrap:TODO launch GTK main loop + create trivial test window");
#endif
}

static void log_touch_stream_stats(const char *reason)
{
    unsigned int dropped = 0;
    unsigned int filtered = 0;
    unsigned int normalized = 0;
    unsigned int lifecycle_forced_cancel = 0;

    gdk_android_input_copy_touch_stream_stats(&dropped,
                                              &filtered,
                                              &normalized,
                                              &lifecycle_forced_cancel);

    LOGI("gtk_bootstrap:touch_stream_stats reason=%s dropped=%u filtered=%u normalized=%u lifecycle_forced_cancel=%u",
         reason ? reason : "unknown",
         dropped,
         filtered,
         normalized,
         lifecycle_forced_cancel);
}

void gdk_android_gtk_bootstrap_handle_event(GdkAndroidEventType type, int arg0, int arg1)
{
    const char *reason = "UNKNOWN";

    switch (type) {
    case GDK_ANDROID_EVENT_LIFECYCLE_CREATE:
        reason = "LIFECYCLE_CREATE";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_CREATE);
        s_state.restore_entry_state_requested = false;
#ifdef GDK_ANDROID_USE_GTK4
        s_state.test_button = NULL;
        s_state.test_toggle = NULL;
        s_state.test_button_click_count = 0;
        s_state.test_toggle_active = false;
#endif
        s_state.gtk_started = false;
        s_state.gtk_init_failed = false;
        s_state.gtk_backend_hint_logged = false;
        s_state.width = 0;
        s_state.height = 0;
        s_state.configured_surface_generation = 0;
        break;
    case GDK_ANDROID_EVENT_PROCESS_RESTORE_HINT:
        reason = "PROCESS_RESTORE_HINT";
        s_state.restore_entry_state_requested =
            (arg0 & GDK_ANDROID_RESTORE_FLAG_SAVED_STATE_PRESENT) != 0 &&
            (arg0 & GDK_ANDROID_RESTORE_FLAG_MATCHING_SAVED_RUN_ID) != 0;
        record_restore_hint_session_state(arg0);
        LOGI("gtk_bootstrap:process_restore_hint flags=0x%x saved_state=%d previous_alive=%d matching_saved_run_id=%d",
             arg0,
             (arg0 & GDK_ANDROID_RESTORE_FLAG_SAVED_STATE_PRESENT) != 0,
             (arg0 & GDK_ANDROID_RESTORE_FLAG_PREVIOUS_RUN_ALIVE) != 0,
             (arg0 & GDK_ANDROID_RESTORE_FLAG_MATCHING_SAVED_RUN_ID) != 0);
        break;
    case GDK_ANDROID_EVENT_LIFECYCLE_RESUME:
        reason = "LIFECYCLE_RESUME";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_RESUME);
        s_state.lifecycle_resumed = true;
        break;
    case GDK_ANDROID_EVENT_LIFECYCLE_PAUSE:
        reason = "LIFECYCLE_PAUSE";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_PAUSE);
        s_state.lifecycle_resumed = false;
        log_touch_stream_stats(reason);
        break;
    case GDK_ANDROID_EVENT_LIFECYCLE_DESTROY:
        reason = "LIFECYCLE_DESTROY";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_DESTROY);
        s_state.lifecycle_resumed = false;
        s_state.surface_available = false;
        s_state.input_ready = false;
        s_state.gtk_started = false;
        s_state.gtk_init_failed = false;
        s_state.gtk_backend_hint_logged = false;
        s_state.width = 0;
        s_state.height = 0;
        s_state.surface_generation = 0;
        s_state.configured_surface_generation = 0;
#ifdef GDK_ANDROID_USE_GTK4
        if (s_state.main_loop != NULL) {
            g_main_loop_quit(s_state.main_loop);
        }
#endif
        log_touch_stream_stats(reason);
        break;
    case GDK_ANDROID_EVENT_ACTIVITY_ATTACHED:
        reason = "ACTIVITY_ATTACHED";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_ACTIVITY_ATTACHED);
        s_state.activity_attached = true;
        LOGI("gtk_bootstrap:activity_attached");
        break;
    case GDK_ANDROID_EVENT_ACTIVITY_DETACHED:
        reason = "ACTIVITY_DETACHED";
        record_lifecycle_session_state(APP_SESSION_LIFECYCLE_ACTIVITY_DETACHED);
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
        s_state.width = 0;
        s_state.height = 0;
        LOGI("gtk_bootstrap:surface_created gen=%d", s_state.surface_generation);
        break;
    case GDK_ANDROID_EVENT_SURFACE_CHANGED:
        reason = "SURFACE_CHANGED";
        s_state.width = arg0;
        s_state.height = arg1;
        if (arg0 > 0 && arg1 > 0 && s_state.surface_available) {
            s_state.configured_surface_generation = s_state.surface_generation;
            record_surface_session_state(arg0, arg1, s_state.surface_generation);
            LOGI("gtk_bootstrap:surface_changed %dx%d gen=%d", arg0, arg1, s_state.surface_generation);
        }
        break;
    case GDK_ANDROID_EVENT_SURFACE_DESTROYED:
        reason = "SURFACE_DESTROYED";
        s_state.surface_available = false;
        s_state.configured_surface_generation = 0;
        s_state.width = 0;
        s_state.height = 0;
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
    case GDK_ANDROID_EVENT_INPUT_MOTION: {
        reason = "INPUT_MOTION";
        int action = 0;
        int pointer_id = -1;
        int action_index = 0;
        int pointer_index = 0;
        float x = 0.f;
        float y = 0.f;
        int pointer_count = 0;
        int active_pointer_count = 0;
           uint64_t event_time_ms = 0;
           uint64_t sequence = 0;
        bool lifecycle_valid = false;
        bool valid = false;

        gdk_android_input_copy_last_motion(&action,
                                           &action_index,
                                           &pointer_index,
                                           &pointer_id,
                                           &x,
                                           &y,
                                           &pointer_count,
                                           &active_pointer_count,
                                       &event_time_ms,
                                       &sequence,
                                           &lifecycle_valid,
                                           &valid);

        if (valid) {
                  LOGI("gtk_bootstrap:input_motion seq=%llu t=%llums lifecycle=%d action=%d actionIndex=%d pointerIndex=%d id=%d x=%.1f y=%.1f pointers=%d active=%d",
                  (unsigned long long)sequence,
                  (unsigned long long)event_time_ms,
                      lifecycle_valid,
                 action,
                 action_index,
                 pointer_index,
                 pointer_id,
                 x,
                 y,
                 pointer_count,
                 active_pointer_count);
        }
        break;
    }
    case GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_UP:
    case GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL: {
        GdkAndroidTouchEvent touch_event;
        bool has_touch_event = gdk_android_input_pop_touch_event(&touch_event);

        switch (type) {
        case GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN:
            reason = "INPUT_TOUCH_DOWN";
            break;
        case GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE:
            reason = "INPUT_TOUCH_MOVE";
            break;
        case GDK_ANDROID_EVENT_INPUT_TOUCH_UP:
            reason = "INPUT_TOUCH_UP";
            break;
        case GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL:
            reason = "INPUT_TOUCH_CANCEL";
            break;
        default:
            reason = "INPUT_TOUCH";
            break;
        }

        if (has_touch_event) {
              LOGI("gtk_bootstrap:%s seq=%llu t=%llums lifecycle=%d id=%d x=%.1f y=%.1f action=%d actionIndex=%d pointerIndex=%d pointers=%d active=%d",
                 reason,
                  (unsigned long long)touch_event.sequence,
                  (unsigned long long)touch_event.event_time_ms,
                  touch_event.lifecycle_valid,
                 touch_event.pointer_id,
                 touch_event.x,
                 touch_event.y,
                 touch_event.action,
                 touch_event.action_index,
                 touch_event.pointer_index,
                 touch_event.pointer_count,
                 touch_event.active_pointer_count);
        } else {
            LOGI("gtk_bootstrap:%s pointerId=%d active=%d (fallback)", reason, arg0, arg1);
        }
        break;
    }
    case GDK_ANDROID_EVENT_IME_SHOW:
        reason = "IME_SHOW";
        LOGI("gtk_bootstrap:ime_show requested");
        break;
    case GDK_ANDROID_EVENT_IME_HIDE:
        reason = "IME_HIDE";
        LOGI("gtk_bootstrap:ime_hide requested");
        break;
    case GDK_ANDROID_EVENT_IME_COMMIT: {
        reason = "IME_COMMIT";
        char committed[IME_LOG_BUFFER_MAX];
        gdk_android_input_copy_last_commit_text(committed, sizeof(committed));
        LOGI("gtk_bootstrap:ime_commit len=%d text='%s'", arg0, committed);
        break;
    }
    default:
        break;
    }

    try_start_gtk(reason);
}