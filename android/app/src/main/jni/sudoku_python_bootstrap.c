/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "sudoku_python_bootstrap.h"

#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TAG "SudokuPython"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef void (*Py_InitializeExFn)(int);
typedef int (*Py_IsInitializedFn)(void);
typedef int (*PyRun_SimpleStringFn)(const char *);

typedef struct {
    void *libpython;
    Py_InitializeExFn initialize_ex;
    Py_IsInitializedFn is_initialized;
    PyRun_SimpleStringFn run_simple_string;
} PythonApi;

static bool
resolve_python_api(PythonApi *api, const char *python_root_dir)
{
    static const char *lib_names[] = {
        "libpython3.13.so",
        "libpython3.12.so",
        "libpython3.11.so",
        "libpython3.10.so",
        "libpython3.9.so",
    };
    char libpath[1024];
    size_t i;

    memset(api, 0, sizeof(*api));

    for (i = 0; i < sizeof(lib_names) / sizeof(lib_names[0]); i++) {
        /* Try full path in python/libs/ first to avoid Android linker preloading */
        if (python_root_dir != NULL && python_root_dir[0] != '\0') {
            snprintf(libpath, sizeof(libpath), "%s/libs/%s", python_root_dir, lib_names[i]);
            api->libpython = dlopen(libpath, RTLD_NOW | RTLD_LOCAL);
        }
        if (api->libpython == NULL) {
            api->libpython = dlopen(lib_names[i], RTLD_NOW | RTLD_LOCAL);
        }
        if (api->libpython != NULL) {
            LOGI("Loaded %s", lib_names[i]);
            break;
        }
    }

    if (api->libpython == NULL) {
        LOGE("Could not load libpython*.so from runtime libs");
        return false;
    }

    api->initialize_ex = (Py_InitializeExFn)dlsym(api->libpython, "Py_InitializeEx");
    api->is_initialized = (Py_IsInitializedFn)dlsym(api->libpython, "Py_IsInitialized");
    api->run_simple_string = (PyRun_SimpleStringFn)dlsym(api->libpython, "PyRun_SimpleString");

    if (api->initialize_ex == NULL || api->is_initialized == NULL || api->run_simple_string == NULL) {
        LOGE("Missing required CPython symbols");
        dlclose(api->libpython);
        memset(api, 0, sizeof(*api));
        return false;
    }

    return true;
}

bool
sudoku_python_try_bootstrap(const char *python_root_dir)
{
    PythonApi api;
    char pythonpath[1024];
    char stdlib_zip[1024];
    DIR *dir;
    struct dirent *ent;
    bool found_zip = false;
    int rc;

    if (python_root_dir == NULL || python_root_dir[0] == '\0') {
        LOGE("python root dir is empty");
        return false;
    }

    if (!resolve_python_api(&api, python_root_dir)) {
        return false;
    }

    snprintf(stdlib_zip, sizeof(stdlib_zip), "%s/stdlib", python_root_dir);
    dir = opendir(stdlib_zip);
    if (dir != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + (len - 4), ".zip") == 0) {
                snprintf(stdlib_zip, sizeof(stdlib_zip), "%s/stdlib/%s", python_root_dir, ent->d_name);
                found_zip = true;
                break;
            }
        }
        closedir(dir);
    }

    if (found_zip) {
        snprintf(pythonpath, sizeof(pythonpath), "%s/sudoku-src:%s", python_root_dir, stdlib_zip);
    } else {
        snprintf(pythonpath, sizeof(pythonpath), "%s/sudoku-src", python_root_dir);
    }

    setenv("PYTHONHOME", python_root_dir, 1);
    setenv("PYTHONPATH", pythonpath, 1);

    LOGI("Calling Py_InitializeEx (signals=0)...");
    api.initialize_ex(0);
    if (!api.is_initialized()) {
        LOGE("Py_InitializeEx returned but interpreter is not initialized");
        dlclose(api.libpython);
        return false;
    }

    rc = api.run_simple_string(
        "import sys\n"
        "print('Sudoku Python bootstrap OK:', sys.version)\n"
        "print('PYTHONPATH=', sys.path)\n"
        "import main\n"
        "print('Imported sudoku main module from', getattr(main, '__file__', '?'))\n");

    if (rc != 0) {
        LOGE("PyRun_SimpleString failed with rc=%d", rc);
        return false;
    }

    LOGI("Python runtime probe succeeded");
    return true;
}
