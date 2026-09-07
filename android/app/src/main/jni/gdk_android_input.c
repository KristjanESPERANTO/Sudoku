/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gdk_android_input.h"
#include "gdk_android_runtime.h"

#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define TAG "gnome-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define LAST_COMMIT_MAX 256
#define MAX_POINTERS 16
#define TOUCH_EVENT_QUEUE_CAP 128

enum {
    MOTION_ACTION_DOWN = 0,
    MOTION_ACTION_UP = 1,
    MOTION_ACTION_MOVE = 2,
    MOTION_ACTION_CANCEL = 3,
    MOTION_ACTION_POINTER_DOWN = 5,
    MOTION_ACTION_POINTER_UP = 6,
};

typedef struct {
    bool active;
    int pointer_id;
    float x;
    float y;
} PointerState;

typedef struct {
    bool active;
    int pointer_id;
} EmittedPointerState;

static pthread_mutex_t s_input_mutex = PTHREAD_MUTEX_INITIALIZER;
static char s_last_commit[LAST_COMMIT_MAX] = {0};

typedef struct {
    bool valid;
    bool lifecycle_valid;
    uint64_t sequence;
    uint64_t event_time_ms;
    int action;
    int action_index;
    int pointer_index;
    int pointer_id;
    float x;
    float y;
    int pointer_count;
    int active_pointer_count;
} LastMotion;

static LastMotion s_last_motion = {
    .valid = false,
    .lifecycle_valid = false,
    .sequence = 0,
    .event_time_ms = 0,
    .action = 0,
    .action_index = 0,
    .pointer_index = 0,
    .pointer_id = -1,
    .x = 0.f,
    .y = 0.f,
    .pointer_count = 0,
    .active_pointer_count = 0,
};

static GdkAndroidTouchPhase
touch_phase_for_motion_action(int action)
{
    switch (action) {
    case MOTION_ACTION_DOWN:
    case MOTION_ACTION_POINTER_DOWN:
        return GDK_ANDROID_TOUCH_PHASE_DOWN;
    case MOTION_ACTION_MOVE:
        return GDK_ANDROID_TOUCH_PHASE_MOVE;
    case MOTION_ACTION_UP:
    case MOTION_ACTION_POINTER_UP:
        return GDK_ANDROID_TOUCH_PHASE_UP;
    case MOTION_ACTION_CANCEL:
        return GDK_ANDROID_TOUCH_PHASE_CANCEL;
    default:
        return GDK_ANDROID_TOUCH_PHASE_NONE;
    }
}

static PointerState s_pointers[MAX_POINTERS] = {0};

static GdkAndroidTouchEvent s_touch_events[TOUCH_EVENT_QUEUE_CAP] = {0};
static int s_touch_event_head = 0;
static int s_touch_event_size = 0;
static unsigned int s_touch_events_dropped = 0;
static uint64_t s_touch_event_sequence = 0;
static unsigned int s_touch_events_filtered = 0;
static unsigned int s_touch_events_normalized = 0;
static unsigned int s_touch_events_lifecycle_forced_cancel = 0;
static EmittedPointerState s_emitted_pointers[MAX_POINTERS] = {0};

static void
clear_all_pointers_locked(void)
{
    for (int i = 0; i < MAX_POINTERS; i++) {
        s_pointers[i].active = false;
        s_pointers[i].pointer_id = -1;
        s_pointers[i].x = 0.f;
        s_pointers[i].y = 0.f;
    }
}

static void
clear_all_emitted_pointers_locked(void)
{
    for (int i = 0; i < MAX_POINTERS; i++) {
        s_emitted_pointers[i].active = false;
        s_emitted_pointers[i].pointer_id = -1;
    }
}

static int
find_emitted_pointer_slot_locked(int pointer_id)
{
    int free_slot = -1;

    for (int i = 0; i < MAX_POINTERS; i++) {
        if (s_emitted_pointers[i].active && s_emitted_pointers[i].pointer_id == pointer_id) {
            return i;
        }
        if (!s_emitted_pointers[i].active && free_slot < 0) {
            free_slot = i;
        }
    }

    return free_slot;
}

static int
count_active_emitted_pointers_locked(void)
{
    int active_count = 0;

    for (int i = 0; i < MAX_POINTERS; i++) {
        if (s_emitted_pointers[i].active) {
            active_count++;
        }
    }

    return active_count;
}

static uint64_t
monotonic_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool
normalize_touch_event_locked(GdkAndroidTouchEvent *event)
{
    int slot = 0;
    bool emitted_active = false;

    if (event == NULL) {
        return false;
    }

    switch (event->phase) {
    case GDK_ANDROID_TOUCH_PHASE_CANCEL:
        if (count_active_emitted_pointers_locked() == 0) {
            s_touch_events_filtered++;
            return false;
        }

        clear_all_emitted_pointers_locked();
        event->active_pointer_count = 0;
        return true;

    case GDK_ANDROID_TOUCH_PHASE_DOWN:
        slot = find_emitted_pointer_slot_locked(event->pointer_id);
        if (slot < 0) {
            s_touch_events_filtered++;
            return false;
        }

        emitted_active = s_emitted_pointers[slot].active &&
                         s_emitted_pointers[slot].pointer_id == event->pointer_id;
        if (emitted_active) {
            s_touch_events_filtered++;
            return false;
        }

        s_emitted_pointers[slot].active = true;
        s_emitted_pointers[slot].pointer_id = event->pointer_id;
        event->active_pointer_count = count_active_emitted_pointers_locked();
        return true;

    case GDK_ANDROID_TOUCH_PHASE_MOVE:
        slot = find_emitted_pointer_slot_locked(event->pointer_id);
        if (slot < 0) {
            s_touch_events_filtered++;
            return false;
        }

        emitted_active = s_emitted_pointers[slot].active &&
                         s_emitted_pointers[slot].pointer_id == event->pointer_id;
        if (!emitted_active) {
            /* Recover from missing DOWN by upgrading the first MOVE to DOWN. */
            event->phase = GDK_ANDROID_TOUCH_PHASE_DOWN;
            event->lifecycle_valid = false;
            s_touch_events_normalized++;
            s_emitted_pointers[slot].active = true;
            s_emitted_pointers[slot].pointer_id = event->pointer_id;
        }

        event->active_pointer_count = count_active_emitted_pointers_locked();
        return true;

    case GDK_ANDROID_TOUCH_PHASE_UP:
        slot = find_emitted_pointer_slot_locked(event->pointer_id);
        if (slot < 0) {
            s_touch_events_filtered++;
            return false;
        }

        emitted_active = s_emitted_pointers[slot].active &&
                         s_emitted_pointers[slot].pointer_id == event->pointer_id;
        if (!emitted_active) {
            s_touch_events_filtered++;
            return false;
        }

        s_emitted_pointers[slot].active = false;
        s_emitted_pointers[slot].pointer_id = -1;
        event->active_pointer_count = count_active_emitted_pointers_locked();
        return true;

    case GDK_ANDROID_TOUCH_PHASE_NONE:
    default:
        s_touch_events_filtered++;
        return false;
    }
}

static GdkAndroidEventType
event_type_for_touch_phase(GdkAndroidTouchPhase phase)
{
    switch (phase) {
    case GDK_ANDROID_TOUCH_PHASE_DOWN:
        return GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN;
    case GDK_ANDROID_TOUCH_PHASE_MOVE:
        return GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE;
    case GDK_ANDROID_TOUCH_PHASE_UP:
        return GDK_ANDROID_EVENT_INPUT_TOUCH_UP;
    case GDK_ANDROID_TOUCH_PHASE_CANCEL:
        return GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL;
    case GDK_ANDROID_TOUCH_PHASE_NONE:
    default:
        return GDK_ANDROID_EVENT_INPUT_MOTION;
    }
}

static bool
is_transition_valid_locked(int action,
                           bool pointer_was_active,
                           int active_before)
{
    switch (action) {
    case MOTION_ACTION_DOWN:
    case MOTION_ACTION_POINTER_DOWN:
        return !pointer_was_active;
    case MOTION_ACTION_MOVE:
        return pointer_was_active;
    case MOTION_ACTION_UP:
    case MOTION_ACTION_POINTER_UP:
        return pointer_was_active;
    case MOTION_ACTION_CANCEL:
        return active_before > 0;
    default:
        return true;
    }
}

static void
enqueue_touch_event_locked(const GdkAndroidTouchEvent *event)
{
    int tail = 0;
    int last_index = 0;
    GdkAndroidTouchEvent *tail_event = NULL;

    if (event == NULL) {
        return;
    }

    if (event->phase == GDK_ANDROID_TOUCH_PHASE_MOVE && s_touch_event_size > 0) {
        last_index = (s_touch_event_head + s_touch_event_size - 1) % TOUCH_EVENT_QUEUE_CAP;
        tail_event = &s_touch_events[last_index];

        if (tail_event->phase == GDK_ANDROID_TOUCH_PHASE_MOVE &&
            tail_event->pointer_id == event->pointer_id) {
            *tail_event = *event;
            return;
        }
    }

    if (s_touch_event_size == TOUCH_EVENT_QUEUE_CAP) {
        s_touch_event_head = (s_touch_event_head + 1) % TOUCH_EVENT_QUEUE_CAP;
        s_touch_event_size--;
        s_touch_events_dropped++;
    }

    tail = (s_touch_event_head + s_touch_event_size) % TOUCH_EVENT_QUEUE_CAP;
    s_touch_events[tail] = *event;
    s_touch_event_size++;
}

static int
find_pointer_slot_locked(int pointer_id)
{
    int free_slot = -1;

    for (int i = 0; i < MAX_POINTERS; i++) {
        if (s_pointers[i].active && s_pointers[i].pointer_id == pointer_id)
            return i;
        if (!s_pointers[i].active && free_slot < 0)
            free_slot = i;
    }

    return free_slot;
}

static int
count_active_pointers_locked(void)
{
    int active_count = 0;
    for (int i = 0; i < MAX_POINTERS; i++) {
        if (s_pointers[i].active)
            active_count++;
    }
    return active_count;
}

void gdk_android_input_init(void)
{
    pthread_mutex_lock(&s_input_mutex);

    clear_all_pointers_locked();
    clear_all_emitted_pointers_locked();
    memset(&s_last_motion, 0, sizeof(s_last_motion));
    s_last_motion.pointer_id = -1;
    s_last_motion.valid = false;

    s_touch_event_head = 0;
    s_touch_event_size = 0;
    s_touch_events_dropped = 0;
    s_touch_events_filtered = 0;
    s_touch_events_normalized = 0;
    s_touch_events_lifecycle_forced_cancel = 0;

    pthread_mutex_unlock(&s_input_mutex);

    LOGI("input:init");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_INPUT_INIT, 0, 0);
}

void gdk_android_input_ime_show(void)
{
    LOGI("input:ime_show");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_IME_SHOW, 0, 0);
}

void gdk_android_input_ime_hide(void)
{
    LOGI("input:ime_hide");
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_IME_HIDE, 0, 0);
}

void gdk_android_input_cancel_active_touches(const char *reason)
{
    int active_before = 0;
    int emitted_before = 0;
    GdkAndroidTouchEvent touch_event = {
        .phase = GDK_ANDROID_TOUCH_PHASE_CANCEL,
        .lifecycle_valid = true,
        .sequence = 0,
        .event_time_ms = 0,
        .action = MOTION_ACTION_CANCEL,
        .action_index = 0,
        .pointer_index = 0,
        .pointer_id = -1,
        .x = 0.f,
        .y = 0.f,
        .pointer_count = 0,
        .active_pointer_count = 0,
    };

    pthread_mutex_lock(&s_input_mutex);

    active_before = count_active_pointers_locked();
    emitted_before = count_active_emitted_pointers_locked();

    if (active_before == 0 && emitted_before == 0) {
        pthread_mutex_unlock(&s_input_mutex);
        return;
    }

    clear_all_pointers_locked();
    clear_all_emitted_pointers_locked();
    s_touch_events_lifecycle_forced_cancel++;

    s_last_motion.valid = true;
    s_last_motion.lifecycle_valid = true;
    s_last_motion.sequence = ++s_touch_event_sequence;
    s_last_motion.event_time_ms = monotonic_time_ms();
    s_last_motion.action = MOTION_ACTION_CANCEL;
    s_last_motion.action_index = 0;
    s_last_motion.pointer_index = 0;
    s_last_motion.pointer_id = -1;
    s_last_motion.x = 0.f;
    s_last_motion.y = 0.f;
    s_last_motion.pointer_count = 0;
    s_last_motion.active_pointer_count = 0;

    touch_event.sequence = s_last_motion.sequence;
    touch_event.event_time_ms = s_last_motion.event_time_ms;
    enqueue_touch_event_locked(&touch_event);

    pthread_mutex_unlock(&s_input_mutex);

    LOGI("input:lifecycle_cancel reason=%s active_before=%d emitted_before=%d seq=%llu t=%llums",
         reason ? reason : "unknown",
         active_before,
         emitted_before,
         (unsigned long long)s_last_motion.sequence,
         (unsigned long long)s_last_motion.event_time_ms);

    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL, -1, 0);
}

void gdk_android_input_motion(int action,
                              int action_index,
                              int pointer_index,
                              int pointer_id,
                              float x,
                              float y,
                              int pointer_count,
                              uint64_t event_time_ms)
{
    int slot = -1;
    bool pointer_was_active = false;
    bool lifecycle_valid = true;
    bool emit_touch_event = false;
    int active_before = 0;
    GdkAndroidEventType touch_event_type = GDK_ANDROID_EVENT_INPUT_MOTION;
    GdkAndroidTouchPhase phase = GDK_ANDROID_TOUCH_PHASE_NONE;
    GdkAndroidTouchEvent touch_event = {
        .phase = GDK_ANDROID_TOUCH_PHASE_NONE,
        .lifecycle_valid = false,
        .sequence = 0,
        .event_time_ms = 0,
        .action = 0,
        .action_index = 0,
        .pointer_index = 0,
        .pointer_id = -1,
        .x = 0.f,
        .y = 0.f,
        .pointer_count = 0,
        .active_pointer_count = 0,
    };

    pthread_mutex_lock(&s_input_mutex);

    active_before = count_active_pointers_locked();

    slot = find_pointer_slot_locked(pointer_id);
    if (slot >= 0 && s_pointers[slot].active && s_pointers[slot].pointer_id == pointer_id) {
        pointer_was_active = true;
    }

    lifecycle_valid = is_transition_valid_locked(action, pointer_was_active, active_before);

    if (action == MOTION_ACTION_CANCEL) {
        clear_all_pointers_locked();
        slot = -1;
    }

    if (slot >= 0) {
        s_pointers[slot].pointer_id = pointer_id;
        s_pointers[slot].x = x;
        s_pointers[slot].y = y;

        switch (action) {
        case MOTION_ACTION_DOWN:
        case MOTION_ACTION_POINTER_DOWN:
        case MOTION_ACTION_MOVE:
            s_pointers[slot].active = true;
            break;
        case MOTION_ACTION_UP:
        case MOTION_ACTION_POINTER_UP:
            if (pointer_index == action_index)
                s_pointers[slot].active = false;
            break;
        case MOTION_ACTION_CANCEL:
            s_pointers[slot].active = false;
            break;
        default:
            break;
        }
    }

    s_last_motion.valid = true;
    s_last_motion.lifecycle_valid = lifecycle_valid;
    s_last_motion.sequence = ++s_touch_event_sequence;
    s_last_motion.event_time_ms = event_time_ms;
    s_last_motion.action = action;
    s_last_motion.action_index = action_index;
    s_last_motion.pointer_index = pointer_index;
    s_last_motion.pointer_id = pointer_id;
    s_last_motion.x = x;
    s_last_motion.y = y;
    s_last_motion.pointer_count = pointer_count;
    s_last_motion.active_pointer_count = count_active_pointers_locked();

    phase = touch_phase_for_motion_action(action);
    if (phase != GDK_ANDROID_TOUCH_PHASE_NONE) {
        touch_event.phase = phase;
        touch_event.lifecycle_valid = lifecycle_valid;
        touch_event.sequence = s_last_motion.sequence;
        touch_event.event_time_ms = event_time_ms;
        touch_event.action = action;
        touch_event.action_index = action_index;
        touch_event.pointer_index = pointer_index;
        touch_event.pointer_id = pointer_id;
        touch_event.x = x;
        touch_event.y = y;
        touch_event.pointer_count = pointer_count;
        touch_event.active_pointer_count = s_last_motion.active_pointer_count;

        if (normalize_touch_event_locked(&touch_event)) {
            touch_event_type = event_type_for_touch_phase(touch_event.phase);
            enqueue_touch_event_locked(&touch_event);
            emit_touch_event = true;
        }
    }

    pthread_mutex_unlock(&s_input_mutex);

        LOGI("input:motion seq=%llu t=%llums lifecycle=%d action=%d actionIndex=%d pointerIndex=%d id=%d x=%.1f y=%.1f pointers=%d active=%d filtered=%u normalized=%u",
         (unsigned long long)s_last_motion.sequence,
         (unsigned long long)s_last_motion.event_time_ms,
            s_last_motion.lifecycle_valid,
         action,
         action_index,
         pointer_index,
         pointer_id,
         x,
         y,
         pointer_count,
         s_last_motion.active_pointer_count,
         s_touch_events_filtered,
         s_touch_events_normalized);
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_INPUT_MOTION, action, pointer_count);
    if (emit_touch_event) {
        gdk_android_runtime_push_event(touch_event_type, pointer_id, touch_event.active_pointer_count);
    }
}

void gdk_android_input_commit_text(const char *text)
{
    size_t len = 0;

    if (text != NULL) {
        len = strlen(text);
    }

    pthread_mutex_lock(&s_input_mutex);
    if (text == NULL || len == 0) {
        s_last_commit[0] = '\0';
    } else {
        size_t n = len;
        if (n >= LAST_COMMIT_MAX) {
            n = LAST_COMMIT_MAX - 1;
        }
        memcpy(s_last_commit, text, n);
        s_last_commit[n] = '\0';
    }
    pthread_mutex_unlock(&s_input_mutex);

    LOGI("input:ime_commit len=%zu", len);
    gdk_android_runtime_push_event(GDK_ANDROID_EVENT_IME_COMMIT, (int)len, 0);
}

void gdk_android_input_copy_last_commit_text(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    pthread_mutex_lock(&s_input_mutex);
    strncpy(buffer, s_last_commit, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    pthread_mutex_unlock(&s_input_mutex);
}

void gdk_android_input_copy_last_motion(int *action,
                                        int *action_index,
                                        int *pointer_index,
                                        int *pointer_id,
                                        float *x,
                                        float *y,
                                        int *pointer_count,
                                        int *active_pointer_count,
                                        uint64_t *event_time_ms,
                                        uint64_t *sequence,
                                        bool *lifecycle_valid,
                                        bool *valid)
{
    pthread_mutex_lock(&s_input_mutex);

    if (action != NULL) {
        *action = s_last_motion.action;
    }
    if (action_index != NULL) {
        *action_index = s_last_motion.action_index;
    }
    if (pointer_index != NULL) {
        *pointer_index = s_last_motion.pointer_index;
    }
    if (pointer_id != NULL) {
        *pointer_id = s_last_motion.pointer_id;
    }
    if (x != NULL) {
        *x = s_last_motion.x;
    }
    if (y != NULL) {
        *y = s_last_motion.y;
    }
    if (pointer_count != NULL) {
        *pointer_count = s_last_motion.pointer_count;
    }
    if (active_pointer_count != NULL) {
        *active_pointer_count = s_last_motion.active_pointer_count;
    }
    if (event_time_ms != NULL) {
        *event_time_ms = s_last_motion.event_time_ms;
    }
    if (sequence != NULL) {
        *sequence = s_last_motion.sequence;
    }
    if (lifecycle_valid != NULL) {
        *lifecycle_valid = s_last_motion.lifecycle_valid;
    }
    if (valid != NULL) {
        *valid = s_last_motion.valid;
    }

    pthread_mutex_unlock(&s_input_mutex);
}

bool gdk_android_input_pop_touch_event(GdkAndroidTouchEvent *event)
{
    bool found = false;

    pthread_mutex_lock(&s_input_mutex);

    if (s_touch_event_size > 0) {
        if (event != NULL) {
            *event = s_touch_events[s_touch_event_head];
        }
        s_touch_event_head = (s_touch_event_head + 1) % TOUCH_EVENT_QUEUE_CAP;
        s_touch_event_size--;
        found = true;
    }

    pthread_mutex_unlock(&s_input_mutex);

    if (!found && event != NULL) {
        memset(event, 0, sizeof(*event));
        event->phase = GDK_ANDROID_TOUCH_PHASE_NONE;
        event->pointer_id = -1;
    }

    return found;
}

void gdk_android_input_copy_touch_stream_stats(unsigned int *dropped,
                                               unsigned int *filtered,
                                               unsigned int *normalized,
                                               unsigned int *lifecycle_forced_cancel)
{
    pthread_mutex_lock(&s_input_mutex);

    if (dropped != NULL) {
        *dropped = s_touch_events_dropped;
    }
    if (filtered != NULL) {
        *filtered = s_touch_events_filtered;
    }
    if (normalized != NULL) {
        *normalized = s_touch_events_normalized;
    }
    if (lifecycle_forced_cancel != NULL) {
        *lifecycle_forced_cancel = s_touch_events_lifecycle_forced_cancel;
    }

    pthread_mutex_unlock(&s_input_mutex);
}
