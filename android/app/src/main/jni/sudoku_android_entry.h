/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef SUDOKU_ANDROID_ENTRY_H
#define SUDOKU_ANDROID_ENTRY_H

#include <glib.h>

/**
 * SECTION:sudoku-android-entry
 * @Short_description: Android entry point for Sudokug game logic
 *
 * Provides JNI-friendly C API to Sudokug Vala game engine for Android platform.
 *
 * Usage:
 * 1. Call sudoku_android_entry_init() once from Activity.onCreate()
 * 2. Call sudoku_android_entry_new_game() to start a game
 * 3. Call sudoku_android_entry_select_tile() for touch input
 * 4. Call sudoku_android_entry_finalize() from Activity.onDestroy()
 */

G_BEGIN_DECLS

/* ──────────────────────────────────────────────────────────── */
/* Lifecycle Management                                          */
/* ──────────────────────────────────────────────────────────── */

void
sudoku_android_entry_init(void);

void
sudoku_android_entry_finalize(void);

/* ──────────────────────────────────────────────────────────── */
/* Game Lifecycle                                                */
/* ──────────────────────────────────────────────────────────── */

gboolean
sudoku_android_entry_new_game(const char *layout_name, int32_t seed);

int
sudoku_android_entry_get_tile_count(void);

void
sudoku_android_entry_set_paused(gboolean paused);

gboolean
sudoku_android_entry_is_paused(void);

/* ──────────────────────────────────────────────────────────── */
/* Touch Input                                                   */
/* ──────────────────────────────────────────────────────────── */

gboolean
sudoku_android_entry_select_tile(int tile_index);

void
sudoku_android_entry_get_last_selection(int *out_tile_0, int *out_tile_1);

unsigned int
sudoku_android_entry_get_move_count(void);

int
sudoku_android_entry_pick_tile_from_surface(float x, float y, int width, int height);

void
sudoku_android_entry_get_grid_shape(int *out_cols, int *out_rows, int *out_tiles);

/* ──────────────────────────────────────────────────────────── */
/* Signal Bridge / Event Handlers                                */
/* ──────────────────────────────────────────────────────────– */

typedef void (*SudokuMovedCallback)(void *user_data);

void
sudoku_android_entry_connect_on_moved(SudokuMovedCallback callback, void *user_data);

G_END_DECLS

#endif /* SUDOKU_ANDROID_ENTRY_H */
