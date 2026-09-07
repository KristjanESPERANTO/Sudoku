/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gdk_android_runtime.h"
#include "gdk_android_gtk_bootstrap.h"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
#include <glib.h>
#endif

#define TAG "gnome-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define QUEUE_CAPACITY 64

typedef struct {
    GdkAndroidEventType type;
    int arg0;
    int arg1;
} RuntimeEvent;

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    sem_t event_sem;
    RuntimeEvent queue[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    bool running;
    bool initialized;
    GdkAndroidRuntimeDispatchFn dispatcher;
    void *dispatcher_userdata;
} RuntimeState;

static RuntimeState s_runtime = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .running = false,
    .initialized = false,
    .dispatcher = NULL,
    .dispatcher_userdata = NULL,
};

static JavaVM *s_java_vm = NULL;

typedef struct {
    JNIEnv *env;
    bool attached_by_runtime;
} ThreadJniEnv;

static pthread_key_t s_thread_jni_key;
static pthread_once_t s_thread_jni_key_once = PTHREAD_ONCE_INIT;

static void
thread_jni_env_destructor(void *data)
{
    ThreadJniEnv *thread_env = (ThreadJniEnv *)data;

    if (thread_env == NULL)
        return;

    if (thread_env->attached_by_runtime && s_java_vm != NULL)
        (*s_java_vm)->DetachCurrentThread(s_java_vm);

    free(thread_env);
}

static void
runtime_init_thread_jni_key_once(void)
{
    (void)pthread_key_create(&s_thread_jni_key, thread_jni_env_destructor);
}

static void
runtime_ensure_thread_jni_key(void)
{
    (void)pthread_once(&s_thread_jni_key_once, runtime_init_thread_jni_key_once);
}

JNIEnv *
gdk_android_runtime_get_env(void)
{
    ThreadJniEnv *thread_env;
    JNIEnv *env = NULL;
    jint rc;

    if (s_java_vm == NULL)
        return NULL;

    runtime_ensure_thread_jni_key();

    thread_env = (ThreadJniEnv *)pthread_getspecific(s_thread_jni_key);
    if (thread_env != NULL && thread_env->env != NULL)
        return thread_env->env;

    rc = (*s_java_vm)->GetEnv(s_java_vm, (void **)&env, JNI_VERSION_1_6);
    if (rc == JNI_OK)
        return env;

    if (rc != JNI_EDETACHED)
        return NULL;

    {
        JavaVMAttachArgs args = {
            .version = JNI_VERSION_1_6,
            .name = "GNOME Android Runtime",
            .group = NULL,
        };

        rc = (*s_java_vm)->AttachCurrentThread(s_java_vm, &env, &args);
        if (rc != JNI_OK)
            return NULL;
    }

    thread_env = (ThreadJniEnv *)calloc(1, sizeof(*thread_env));
    if (thread_env == NULL)
        {
            (*s_java_vm)->DetachCurrentThread(s_java_vm);
            return NULL;
        }

    thread_env->env = env;
    thread_env->attached_by_runtime = true;
    (void)pthread_setspecific(s_thread_jni_key, thread_env);

    return env;
}

void
gdk_android_runtime_clear_thread_env(void)
{
    ThreadJniEnv *thread_env;

    runtime_ensure_thread_jni_key();

    thread_env = (ThreadJniEnv *)pthread_getspecific(s_thread_jni_key);
    if (thread_env == NULL)
        return;

    (void)pthread_setspecific(s_thread_jni_key, NULL);
    thread_jni_env_destructor(thread_env);
}

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
/* Forward declarations for queue helpers defined later in this file. */
static bool queue_pop_locked (RuntimeEvent *out);

/* Pipe used to wake up the GLib main loop when Android events are pushed.
 * Initialized lazily by gdk_android_runtime_create_event_source(). */
static int s_event_pipe[2] = { -1, -1 };

typedef struct {
    GSource  source;
    GPollFD  poll_fd;
} AndroidEventSource;

static gboolean
android_event_source_prepare (GSource *source, gint *timeout)
{
    (void)source;
    /* Let the poll-fd determine wakeups; no periodic fallback needed. */
    *timeout = -1;
    return FALSE;
}

static gboolean
android_event_source_check (GSource *source)
{
    AndroidEventSource *self = (AndroidEventSource *)source;
    return (self->poll_fd.revents & G_IO_IN) != 0;
}

static gboolean
android_event_source_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
    (void)callback;
    (void)user_data;
    AndroidEventSource *self = (AndroidEventSource *)source;

    /* Drain the pipe so the fd stops signalling. */
    char buf[64];
    while (read (self->poll_fd.fd, buf, sizeof (buf)) > 0) {}

    /* Pop and dispatch every pending Android event. */
    while (true) {
        RuntimeEvent ev;
        bool has_event = false;

        pthread_mutex_lock (&s_runtime.mutex);
        has_event = queue_pop_locked (&ev);
        pthread_mutex_unlock (&s_runtime.mutex);

        if (!has_event)
            break;

        gdk_android_gtk_bootstrap_handle_event (ev.type, ev.arg0, ev.arg1);
        LOGI ("runtime:gsource-dispatch event=%d arg0=%d arg1=%d", ev.type, ev.arg0, ev.arg1);
    }

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs s_android_event_source_funcs = {
    android_event_source_prepare,
    android_event_source_check,
    android_event_source_dispatch,
    NULL,
    NULL,
    NULL,
};

GSource *
gdk_android_runtime_create_event_source (void)
{
    /* Create the wakeup pipe exactly once. */
    if (s_event_pipe[0] == -1) {
        if (pipe (s_event_pipe) != 0) {
            LOGI ("runtime:failed to create event pipe");
            return NULL;
        }
        int flags;
        flags = fcntl (s_event_pipe[0], F_GETFL, 0);
        fcntl (s_event_pipe[0], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl (s_event_pipe[1], F_GETFL, 0);
        fcntl (s_event_pipe[1], F_SETFL, flags | O_NONBLOCK);
        LOGI ("runtime:event pipe created read_fd=%d write_fd=%d",
              s_event_pipe[0], s_event_pipe[1]);
    }

    AndroidEventSource *src =
        (AndroidEventSource *)g_source_new (&s_android_event_source_funcs,
                                            sizeof (AndroidEventSource));
    src->poll_fd.fd      = s_event_pipe[0];
    src->poll_fd.events  = G_IO_IN;
    src->poll_fd.revents = 0;
    g_source_add_poll ((GSource *)src, &src->poll_fd);
    g_source_set_name ((GSource *)src, "android-event-source");

    return (GSource *)src;
}

typedef struct {
    GdkAndroidEventType type;
    int arg0;
    int arg1;
} GlibDispatchPayload;

static gboolean glib_dispatch_cb(gpointer data)
{
    GlibDispatchPayload *payload = (GlibDispatchPayload *)data;
    gdk_android_gtk_bootstrap_handle_event(payload->type, payload->arg0, payload->arg1);
    LOGI("runtime:glib-dispatch event=%d arg0=%d arg1=%d",
         payload->type,
         payload->arg0,
         payload->arg1);
    g_free(payload);
    return G_SOURCE_REMOVE;
}

static void runtime_dispatch_via_glib(GdkAndroidEventType type, int arg0, int arg1, void *userdata)
{
    GMainContext *context = (GMainContext *)userdata;
    GlibDispatchPayload *payload = g_new0(GlibDispatchPayload, 1);
    payload->type = type;
    payload->arg0 = arg0;
    payload->arg1 = arg1;

    if (context == NULL) {
        context = g_main_context_default();
    }

    g_main_context_invoke_full(context, G_PRIORITY_DEFAULT, glib_dispatch_cb, payload, NULL);
}
#endif

static const char *event_name(GdkAndroidEventType type)
{
    switch (type) {
    case GDK_ANDROID_EVENT_LIFECYCLE_CREATE:
        return "LIFECYCLE_CREATE";
    case GDK_ANDROID_EVENT_PROCESS_RESTORE_HINT:
        return "PROCESS_RESTORE_HINT";
    case GDK_ANDROID_EVENT_LIFECYCLE_PAUSE:
        return "LIFECYCLE_PAUSE";
    case GDK_ANDROID_EVENT_LIFECYCLE_RESUME:
        return "LIFECYCLE_RESUME";
    case GDK_ANDROID_EVENT_LIFECYCLE_DESTROY:
        return "LIFECYCLE_DESTROY";
    case GDK_ANDROID_EVENT_ACTIVITY_ATTACHED:
        return "ACTIVITY_ATTACHED";
    case GDK_ANDROID_EVENT_ACTIVITY_DETACHED:
        return "ACTIVITY_DETACHED";
    case GDK_ANDROID_EVENT_SURFACE_CREATED:
        return "SURFACE_CREATED";
    case GDK_ANDROID_EVENT_SURFACE_CHANGED:
        return "SURFACE_CHANGED";
    case GDK_ANDROID_EVENT_SURFACE_DESTROYED:
        return "SURFACE_DESTROYED";
    case GDK_ANDROID_EVENT_INPUT_INIT:
        return "INPUT_INIT";
    case GDK_ANDROID_EVENT_INPUT_MOTION:
        return "INPUT_MOTION";
    case GDK_ANDROID_EVENT_INPUT_TOUCH_DOWN:
        return "INPUT_TOUCH_DOWN";
    case GDK_ANDROID_EVENT_INPUT_TOUCH_MOVE:
        return "INPUT_TOUCH_MOVE";
    case GDK_ANDROID_EVENT_INPUT_TOUCH_UP:
        return "INPUT_TOUCH_UP";
    case GDK_ANDROID_EVENT_INPUT_TOUCH_CANCEL:
        return "INPUT_TOUCH_CANCEL";
    case GDK_ANDROID_EVENT_IME_SHOW:
        return "IME_SHOW";
    case GDK_ANDROID_EVENT_IME_HIDE:
        return "IME_HIDE";
    case GDK_ANDROID_EVENT_IME_COMMIT:
        return "IME_COMMIT";
    default:
        return "UNKNOWN";
    }
}

static bool queue_push_locked(RuntimeEvent ev)
{
    if (s_runtime.count == QUEUE_CAPACITY) {
        return false;
    }
    s_runtime.queue[s_runtime.tail] = ev;
    s_runtime.tail = (s_runtime.tail + 1) % QUEUE_CAPACITY;
    s_runtime.count++;
    return true;
}

static bool queue_pop_locked(RuntimeEvent *out)
{
    if (s_runtime.count == 0) {
        return false;
    }
    *out = s_runtime.queue[s_runtime.head];
    s_runtime.head = (s_runtime.head + 1) % QUEUE_CAPACITY;
    s_runtime.count--;
    return true;
}

static void *runtime_thread_main(void *userdata)
{
    (void)userdata;
    JNIEnv *env = NULL;

    LOGI("runtime:thread started");
    env = gdk_android_runtime_get_env();
    if (env != NULL)
        LOGI("runtime:thread has JNI env");
    else
        LOGI("runtime:thread has no JNI env (vm not ready or attach failed)");

    while (true) {
        RuntimeEvent ev;
        bool has_event = false;
        bool wake_timeout = false;

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
        {
            struct timespec now;
            struct timespec wake_at;
            int wait_rc;

            clock_gettime(CLOCK_REALTIME, &now);
            wake_at = now;
            wake_at.tv_nsec += 16 * 1000 * 1000;
            if (wake_at.tv_nsec >= 1000 * 1000 * 1000) {
                wake_at.tv_sec += 1;
                wake_at.tv_nsec -= 1000 * 1000 * 1000;
            }

            do {
                wait_rc = sem_timedwait(&s_runtime.event_sem, &wake_at);
            } while (wait_rc != 0 && errno == EINTR);

            if (wait_rc != 0 && errno == ETIMEDOUT)
                wake_timeout = true;
        }
#else
        {
            int wait_rc;
            do {
                wait_rc = sem_wait(&s_runtime.event_sem);
            } while (wait_rc != 0 && errno == EINTR);
        }
#endif

        pthread_mutex_lock(&s_runtime.mutex);
        if (!s_runtime.running && s_runtime.count == 0) {
            pthread_mutex_unlock(&s_runtime.mutex);
            break;
        }

        has_event = queue_pop_locked(&ev);
        pthread_mutex_unlock(&s_runtime.mutex);

        if (!has_event) {
#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
            if (wake_timeout) {
                while (g_main_context_iteration(NULL, FALSE)) {
                }
            }
#endif
            continue;
        }

        GdkAndroidRuntimeDispatchFn dispatcher = NULL;
        void *dispatcher_userdata = NULL;

        pthread_mutex_lock(&s_runtime.mutex);
        dispatcher = s_runtime.dispatcher;
        dispatcher_userdata = s_runtime.dispatcher_userdata;
        pthread_mutex_unlock(&s_runtime.mutex);

        if (dispatcher != NULL) {
            dispatcher(ev.type, ev.arg0, ev.arg1, dispatcher_userdata);
        } else {
            gdk_android_gtk_bootstrap_handle_event(ev.type, ev.arg0, ev.arg1);
            LOGI("runtime:event %s arg0=%d arg1=%d", event_name(ev.type), ev.arg0, ev.arg1);
        }

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
        while (g_main_context_iteration(NULL, FALSE)) {
        }
#endif
    }

    gdk_android_runtime_clear_thread_env();
    LOGI("runtime:thread JNI env cleared");

    LOGI("runtime:thread stopped");
    return NULL;
}

void gdk_android_runtime_init(void)
{
    if (s_runtime.initialized) {
        return;
    }

    pthread_mutex_init(&s_runtime.mutex, NULL);
    sem_init(&s_runtime.event_sem, 0, 0);
    s_runtime.running = true;
    s_runtime.initialized = true;

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
    s_runtime.dispatcher = runtime_dispatch_via_glib;
    s_runtime.dispatcher_userdata = g_main_context_default();
    LOGI("runtime:glib dispatcher enabled");
#endif

    pthread_create(&s_runtime.thread, NULL, runtime_thread_main, NULL);
}

void gdk_android_runtime_shutdown(void)
{
    if (!s_runtime.initialized) {
        return;
    }

    pthread_mutex_lock(&s_runtime.mutex);
    s_runtime.running = false;
    pthread_mutex_unlock(&s_runtime.mutex);

    sem_post(&s_runtime.event_sem);

    pthread_join(s_runtime.thread, NULL);
    sem_destroy(&s_runtime.event_sem);
    pthread_mutex_destroy(&s_runtime.mutex);

    s_runtime.head = 0;
    s_runtime.tail = 0;
    s_runtime.count = 0;
    s_runtime.dispatcher = NULL;
    s_runtime.dispatcher_userdata = NULL;
    s_runtime.initialized = false;
}

void gdk_android_runtime_push_event(GdkAndroidEventType type, int arg0, int arg1)
{
    RuntimeEvent ev = {
        .type = type,
        .arg0 = arg0,
        .arg1 = arg1,
    };

    if (!s_runtime.initialized) {
        return;
    }

    pthread_mutex_lock(&s_runtime.mutex);
    if (!queue_push_locked(ev)) {
        LOGI("runtime:event dropped (queue full): %s", event_name(type));
    } else {
        sem_post(&s_runtime.event_sem);
    }
    pthread_mutex_unlock(&s_runtime.mutex);

#ifdef GDK_ANDROID_USE_GLIB_DISPATCH
    /* Wake up the GLib main loop if the event-source pipe is active. */
    if (s_event_pipe[1] != -1) {
        char byte = 1;
        (void)write(s_event_pipe[1], &byte, 1);
    }
#endif
}

void gdk_android_runtime_set_dispatcher(GdkAndroidRuntimeDispatchFn fn, void *userdata)
{
    pthread_mutex_lock(&s_runtime.mutex);
    s_runtime.dispatcher = fn;
    s_runtime.dispatcher_userdata = userdata;
    pthread_mutex_unlock(&s_runtime.mutex);
}

void gdk_android_runtime_set_java_vm(JavaVM *vm)
{
    s_java_vm = vm;
}