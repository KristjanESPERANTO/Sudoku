/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <jni.h>
#include <android/log.h>
#include <stdbool.h>
#include <stdint.h>

#include "gdk_android_gl_context.h"
#include "gdk_android_input.h"
#include "gdk_android_lifecycle.h"
#include "gdk_android_runtime.h"
#include "sudoku_bootstrap_runtime.h"
#include "sudoku_android_entry.h"
#include "sudoku_python_bootstrap.h"

#ifdef SUDOKU_USE_GTK4
#include <gtk/gtk.h>

gboolean gdk_android_initialize(JNIEnv *env, jobject application_classloader, jobject activity);
void gdk_android_finalize(void);
#endif

#define TAG "SudokuBootstrap"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static jobject s_activity_ref = NULL;
static JavaVM *s_vm = NULL;

static JNIEnv *
get_callback_env(bool *out_attached)
{
    JNIEnv *env = gdk_android_runtime_get_env();

    if (out_attached != NULL) {
        *out_attached = false;
    }

    if (env != NULL) {
        return env;
    }

    if (s_vm == NULL) {
        return NULL;
    }

    if ((*s_vm)->GetEnv(s_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }

    if ((*s_vm)->AttachCurrentThread(s_vm, &env, NULL) != JNI_OK) {
        return NULL;
    }

    if (out_attached != NULL) {
        *out_attached = true;
    }

    return env;
}

static void
notify_activity_board_changed(void *user_data)
{
    JNIEnv *env;
    bool attached = false;
    jclass activity_class;
    jmethodID on_board_changed;
    int selected_0 = -1;
    int selected_1 = -1;
    int tile_count = 0;
    unsigned int move_count;

    (void)user_data;

    if (s_activity_ref == NULL) {
        return;
    }

    env = get_callback_env(&attached);
    if (env == NULL) {
        return;
    }

    activity_class = (*env)->GetObjectClass(env, s_activity_ref);
    if (activity_class == NULL) {
        return;
    }

    on_board_changed = (*env)->GetMethodID(env, activity_class,
                                           "onNativeBoardChanged", "(IIII)V");
    if (on_board_changed == NULL) {
        (*env)->ExceptionClear(env);
        LOGE("jni:onNativeBoardChanged method missing in SudokuActivity");
        return;
    }

    sudoku_android_entry_get_last_selection(&selected_0, &selected_1);
    tile_count = sudoku_android_entry_get_tile_count();
    move_count = sudoku_android_entry_get_move_count();

    (*env)->CallVoidMethod(env, s_activity_ref, on_board_changed,
                           (jint)move_count, (jint)selected_0, (jint)selected_1,
                           (jint)tile_count);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    if (attached && s_vm != NULL) {
        (*s_vm)->DetachCurrentThread(s_vm);
    }
}

#ifdef SUDOKU_USE_GTK4
static bool s_gdk_android_initialized = false;
static bool s_gdk_mode_logged = false;

void sudoku_bootstrap_log_gtk_mode(bool ok)
{
    if (s_gdk_mode_logged) {
        return;
    }

    if (ok) {
        LOGI("Sudokug bootstrap gtk_mode=gtk4");
    } else {
        LOGI("Sudokug bootstrap gtk_mode=gtk4_init_failed");
    }

    s_gdk_mode_logged = true;
}

static bool ensure_gdk_android_initialized(JNIEnv *env, jobject activity)
{
    jclass activity_class;
    jclass class_class;
    jmethodID get_class_loader;
    jobject class_loader;
    gboolean ok;

    if (s_gdk_android_initialized) {
        return true;
    }

    activity_class = (*env)->GetObjectClass(env, activity);
    if (activity_class == NULL) {
        LOGE("jni:gdk_android_initialize failed to get activity class");
        return false;
    }

    class_class = (*env)->FindClass(env, "java/lang/Class");
    if (class_class == NULL) {
        LOGE("jni:gdk_android_initialize failed to find java/lang/Class");
        return false;
    }

    get_class_loader = (*env)->GetMethodID(env, class_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (get_class_loader == NULL) {
        LOGE("jni:gdk_android_initialize failed to resolve getClassLoader");
        return false;
    }

    class_loader = (*env)->CallObjectMethod(env, activity_class, get_class_loader);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        LOGE("jni:gdk_android_initialize getClassLoader threw");
        return false;
    }
    if (class_loader == NULL) {
        LOGE("jni:gdk_android_initialize returned null class loader");
        return false;
    }

    ok = gdk_android_initialize(env, class_loader, activity);
    if (ok) {
        LOGI("Sudokug bootstrap gtk_mode=gtk4");
    } else {
        LOGI("Sudokug bootstrap gtk_mode=gtk4_init_failed");
    }
    s_gdk_android_initialized = true;
    LOGI("jni:gdk_android_initialize succeeded");
    return true;
}
#endif

bool
sudoku_bootstrap_ensure_gdk_android_initialized_on_runtime_thread(void)
{
#ifdef SUDOKU_USE_GTK4
    JNIEnv *env;

    if (s_gdk_android_initialized) {
        return true;
    }

    env = gdk_android_runtime_get_env();
    if (env == NULL) {
        LOGE("jni:gdk init on runtime thread failed: no JNIEnv");
        return false;
    }

    if (s_activity_ref == NULL) {
        LOGE("jni:gdk init on runtime thread failed: activity ref missing");
        return false;
    }

    return ensure_gdk_android_initialized(env, s_activity_ref);
#else
    return false;
#endif
}

static void replace_activity_ref(JNIEnv *env, jobject activity)
{
    if (s_activity_ref != NULL) {
        (*env)->DeleteGlobalRef(env, s_activity_ref);
        s_activity_ref = NULL;
    }

    if (activity != NULL) {
        s_activity_ref = (*env)->NewGlobalRef(env, activity);
        LOGI("jni:activity attached ref=%p", (void *)s_activity_ref);
    }
}

static void clear_activity_ref(JNIEnv *env)
{
    if (s_activity_ref != NULL) {
        LOGI("jni:activity detached ref=%p", (void *)s_activity_ref);
        (*env)->DeleteGlobalRef(env, s_activity_ref);
        s_activity_ref = NULL;
    }
}

/* ──────────────────────────────────────────────────────────── */
/* JNI Entry Points (Called from Kotlin SudokuActivity)        */
/* ──────────────────────────────────────────────────────────── */

JNIEXPORT void JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeOnCreate(JNIEnv *env, jobject object,
                                                       jobject activity)
{
    LOGI("jni:onCreate");
    (void)object;
    replace_activity_ref(env, activity);
    sudoku_android_entry_connect_on_moved(notify_activity_board_changed, NULL);
    sudoku_android_entry_init();
    (void)sudoku_android_entry_new_game(NULL, 0);
}

JNIEXPORT void JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeRequestBoardSync(JNIEnv *env, jobject object)
{
    (void)env;
    (void)object;
    notify_activity_board_changed(NULL);
}

JNIEXPORT jboolean JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeOnTap(JNIEnv *env,
                                                    jobject object,
                                                    jfloat x,
                                                    jfloat y,
                                                    jint width,
                                                    jint height)
{
    int tile_index;

    (void)env;
    (void)object;

    tile_index = sudoku_android_entry_pick_tile_from_surface(x, y, width, height);
    if (tile_index < 0) {
        return JNI_FALSE;
    }

    return sudoku_android_entry_select_tile(tile_index) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeTryPythonBootstrap(JNIEnv *env,
                                                              jobject object,
                                                              jstring pythonRootDir)
{
    const char *path;
    bool ok;

    (void)object;

    if (pythonRootDir == NULL) {
        return JNI_FALSE;
    }

    path = (*env)->GetStringUTFChars(env, pythonRootDir, NULL);
    if (path == NULL) {
        return JNI_FALSE;
    }

    ok = sudoku_python_try_bootstrap(path);
    (*env)->ReleaseStringUTFChars(env, pythonRootDir, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeOnResume(JNIEnv *env, jobject object)
{
    LOGI("jni:onResume");
    (void)env;
    (void)object;
    sudoku_android_entry_set_paused(FALSE);
}

JNIEXPORT void JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeOnPause(JNIEnv *env, jobject object)
{
    LOGI("jni:onPause");
    (void)env;
    (void)object;
    sudoku_android_entry_set_paused(TRUE);
}

JNIEXPORT void JNICALL
Java_org_gnome_sudoku_SudokuActivity_nativeOnDestroy(JNIEnv *env, jobject object)
{
    LOGI("jni:onDestroy");
    (void)object;
    sudoku_android_entry_connect_on_moved(NULL, NULL);
    sudoku_android_entry_finalize();
    gdk_android_runtime_shutdown();
    clear_activity_ref(env);
}

/* ──────────────────────────────────────────────────────────– */
/* JNI_OnLoad (Library initialization)                         */
/* ──────────────────────────────────────────────────────────– */

jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    LOGI("jni:JNI_OnLoad");
    s_vm = vm;
    gdk_android_runtime_set_java_vm(vm);
    gdk_android_runtime_init();
    (void)reserved;
    return JNI_VERSION_1_6;
}
