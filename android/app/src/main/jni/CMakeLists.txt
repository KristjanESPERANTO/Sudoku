cmake_minimum_required(VERSION 3.22.1)
project(sudoku_bootstrap C)

find_package(PkgConfig)

option(SUDOKU_ENABLE_GTK4 "Link GTK4 runtime pipeline into sudoku bootstrap" OFF)

get_filename_component(ANDROID_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
get_filename_component(SUDOKU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(A0_REPO_ROOT "${SUDOKU_REPO_ROOT}")
include("${ANDROID_PROJECT_ROOT}/cmake/A0Paths.cmake")

set(SUDOKU_ENTRY_LIBRARY "${A0_C1_OVERLAY_PREFIX}/lib/libgnome_sudoku_entry.so")

if(EXISTS "${A0_BASELINE_PREFIX}/lib/pkgconfig")
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        set(ENV{PKG_CONFIG_PATH} "${A0_BASELINE_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${A0_BASELINE_PREFIX}/lib/pkgconfig")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${A0_BASELINE_PREFIX}")
endif()

set(APP_JNI_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(
    sudoku_bootstrap
    SHARED
    sudoku_bootstrap.c
    sudoku_android_entry.c
    sudoku_python_bootstrap.c
)

target_include_directories(
    sudoku_bootstrap
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${APP_JNI_DIR}"
)

target_link_libraries(
    sudoku_bootstrap
    android
    log
    EGL
    GLESv2
)

# Always link GLib for entry point
if(PkgConfig_FOUND)
    pkg_check_modules(GLIB2 glib-2.0)

    if(GLIB2_FOUND)
        string(REPLACE "/workspace/.a0-baseline/install" "${A0_BASELINE_PREFIX}" GLIB2_CFLAGS_SANITIZED "${GLIB2_CFLAGS}")
        string(REPLACE "/workspace/.a0-baseline/install" "${A0_BASELINE_PREFIX}" GLIB2_LDFLAGS_SANITIZED "${GLIB2_LDFLAGS}")

        target_compile_options(sudoku_bootstrap PRIVATE ${GLIB2_CFLAGS_SANITIZED})
        target_link_options(sudoku_bootstrap PRIVATE ${GLIB2_LDFLAGS_SANITIZED})

        message(STATUS "GLib found: linking for sudoku entry point")
    else()
        message(WARNING "GLib not found via pkg-config; sudoku entry point may not link")
    endif()
endif()

if(PkgConfig_FOUND AND SUDOKU_ENABLE_GTK4)
    set(PKG_CONFIG_ARGN "--define-variable=prefix=${A0_BASELINE_PREFIX}")

    pkg_check_modules(GTK4  gtk4)

    if(GLIB2_FOUND AND GTK4_FOUND)
        string(REPLACE "/workspace/.a0-baseline/install" "${A0_BASELINE_PREFIX}" GTK4_CFLAGS_SANITIZED "${GTK4_CFLAGS}")
        string(REPLACE "/workspace/.a0-baseline/install" "${A0_BASELINE_PREFIX}" GTK4_LDFLAGS_SANITIZED "${GTK4_LDFLAGS}")

        target_compile_options(sudoku_bootstrap PRIVATE ${GTK4_CFLAGS_SANITIZED})
        target_link_options(sudoku_bootstrap PRIVATE ${GTK4_LDFLAGS_SANITIZED})

        target_sources(
            sudoku_bootstrap
            PRIVATE
            "${APP_JNI_DIR}/gdk_android_runtime.c"
            sudoku_gtk_bootstrap.c
            "${APP_JNI_DIR}/gdk_android_lifecycle.c"
            "${APP_JNI_DIR}/gdk_android_input.c"
            "${APP_JNI_DIR}/gdk_android_gl_context.c"
        )

        target_compile_definitions(
            sudoku_bootstrap
            PRIVATE
            SUDOKU_USE_GTK4=1
            GDK_ANDROID_USE_GTK4=1
            GDK_ANDROID_USE_GLIB_DISPATCH=1
        )

        if(EXISTS "${SUDOKU_ENTRY_LIBRARY}")
            target_link_libraries(sudoku_bootstrap "${SUDOKU_ENTRY_LIBRARY}")
            target_compile_definitions(sudoku_bootstrap PRIVATE SUDOKU_HAS_ENTRY_MAIN=1)
            message(STATUS "gnome-sudoku entry library found: ${SUDOKU_ENTRY_LIBRARY}")
        else()
            message(WARNING "gnome-sudoku entry library missing: ${SUDOKU_ENTRY_LIBRARY}")
        endif()
    else()
        message(WARNING "SUDOKU_ENABLE_GTK4=ON but required glib/gtk4 packages were not found")
    endif()
endif()
