/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Android entry point for Sudokug game logic.
 *
 * Wraps Vala-generated Game/Map/GameSave classes and provides:
 * - GLib event loop integration (for timers, signals)
 * - Game object lifecycle management
 * - Tile rendering signal forwarding
 * - Touch input API for JNI bridge
 */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <android/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>

/* Forward declarations for Vala-generated classes */
/* (Would be in generated headers: gnome_sudoku.h) */
typedef struct _Game Game;
typedef struct _Map Map;
typedef struct _GameSave GameSave;
typedef struct _Tile Tile;

#define TAG "SudokuEntry"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ──────────────────────────────────────────────────────────── */
/* Global Game State                                            */
/* ──────────────────────────────────────────────────────────── */

typedef struct {
    Game            *game;
    GMainContext    *main_ctx;
    GMainLoop       *main_loop;
    bool             game_started;
    bool             game_paused;
    int              selected_tile_0;  /* -1 = none */
    int              selected_tile_1;  /* -1 = none */
    unsigned int     move_count;
    int              fallback_tile_count;
    bool             fallback_mode;
} SudokuEntryState;

static SudokuEntryState s_state = {
    .game               = NULL,
    .main_ctx           = NULL,
    .main_loop          = NULL,
    .game_started       = false,
    .game_paused        = false,
    .selected_tile_0    = -1,
    .selected_tile_1    = -1,
    .move_count         = 0,
    .fallback_tile_count = 96,
    .fallback_mode      = false,
};

typedef void (*SudokuMovedCallback)(void *user_data);

static SudokuMovedCallback s_on_moved_cb = NULL;
static void *s_on_moved_user_data = NULL;

static void
dispatch_board_changed(void)
{
    if (s_on_moved_cb != NULL) {
        s_on_moved_cb(s_on_moved_user_data);
    }
}

static void
compute_grid_shape(int tile_count, int *out_cols, int *out_rows)
{
    int cols = 1;
    int rows;

    if (tile_count <= 0) {
        tile_count = 96;
    }

    while (cols * cols < tile_count) {
        cols++;
    }

    rows = (tile_count + cols - 1) / cols;

    if (out_cols != NULL) {
        *out_cols = cols;
    }
    if (out_rows != NULL) {
        *out_rows = rows;
    }
}

/* ──────────────────────────────────────────────────────────── */
/* Placeholder Vala bindings (would be auto-generated)          */
/* ──────────────────────────────────────────────────────────── */

/* In Phase 2+: Replace these with actual #include of generated headers */

typedef struct {
    int32_t x, y, z;
} TilePos;

typedef enum {
    TILE_BACK = 0,
    TILE_BAMBOO_1 = 1,
    /* ... other tile types (would come from Vala enum) */
} TileType;

struct _Tile {
    TilePos pos;
    TileType type;
    bool exposed;
};

struct _Game {
    GObject parent;
    Tile *tiles;
    int n_tiles;
    void (*signal_moved)(Game *self);
    void (*signal_paused_changed)(Game *self);
};

struct _Map {
    GObject parent;
    char *name;
    int width, height, depth;
};

typedef Game* (*game_new_fn)(Map *map);
typedef void (*game_generate_fn)(Game *self, int32_t seed);
typedef gboolean (*game_remove_pair_fn)(Game *self, Tile *t0, Tile *t1);
typedef gboolean (*game_get_paused_fn)(Game *self);
typedef void (*game_set_paused_fn)(Game *self, gboolean value);
typedef int (*game_get_n_tiles_fn)(Game *self);
typedef Tile* (*game_get_tile_fn)(Game *self, int idx);
typedef Map* (*maps_load_fn)(void);

/* Stub implementations (Phase 2: link real Vala-generated exports) */
static game_new_fn real_game_new = NULL;
static game_generate_fn real_game_generate = NULL;
static game_remove_pair_fn real_game_remove_pair = NULL;
static game_get_paused_fn real_game_get_paused = NULL;
static game_set_paused_fn real_game_set_paused = NULL;
static game_get_n_tiles_fn real_game_get_n_tiles = NULL;
static game_get_tile_fn real_game_get_tile = NULL;
static maps_load_fn real_maps_load = NULL;

/* ──────────────────────────────────────────────────────────── */
/* GLib Event Loop Management                                    */
/* ──────────────────────────────────────────────────────────── */

/**
 * sudoku_android_entry_init:
 *
 * Initialize GLib event loop and game state.
 * Call once from Android Activity.onCreate().
 */
void
sudoku_android_entry_init(void)
{
    if (s_state.main_ctx != NULL) {
        LOGI("entry:init: already initialized");
        return;
    }

    LOGI("entry:init: starting GLib event loop");

    /* Create dedicated GMainContext for game thread */
    s_state.main_ctx = g_main_context_new();
    if (s_state.main_ctx == NULL) {
        LOGE("entry:init: failed to create GMainContext");
        return;
    }

    /* Set thread-local context for this thread */
    g_main_context_push_thread_default(s_state.main_ctx);

    /* Create main loop (not running yet; run on game thread in Phase 2) */
    s_state.main_loop = g_main_loop_new(s_state.main_ctx, FALSE);
    if (s_state.main_loop == NULL) {
        LOGE("entry:init: failed to create GMainLoop");
        g_main_context_pop_thread_default(s_state.main_ctx);
        g_object_unref(s_state.main_ctx);
        s_state.main_ctx = NULL;
        return;
    }

    LOGI("entry:init: GLib context ready");
}

/**
 * sudoku_android_entry_finalize:
 *
 * Clean up game and GLib event loop.
 * Call from Android Activity.onDestroy().
 */
void
sudoku_android_entry_finalize(void)
{
    if (s_state.main_loop != NULL) {
        g_main_loop_unref(s_state.main_loop);
        s_state.main_loop = NULL;
    }

    if (s_state.main_ctx != NULL) {
        g_main_context_pop_thread_default(s_state.main_ctx);
        g_object_unref(s_state.main_ctx);
        s_state.main_ctx = NULL;
    }

    if (s_state.game != NULL) {
        g_object_unref(s_state.game);
        s_state.game = NULL;
    }

    s_state.fallback_mode = false;
    s_state.game_started = false;
    s_state.game_paused = false;
    s_state.selected_tile_0 = -1;
    s_state.selected_tile_1 = -1;
    s_state.move_count = 0;

    LOGI("entry:finalize: cleaned up");
}

/* ──────────────────────────────────────────────────────────── */
/* Game Lifecycle API                                            */
/* ──────────────────────────────────────────────────────────── */

/**
 * sudoku_android_entry_new_game:
 * @layout_name: (nullable): Map name (e.g., "default"). If NULL, use first available.
 * @seed: Random seed for tile generation. Use 0 for random.
 *
 * Create a new game with specified layout and seed.
 * Returns: true on success, false if game state invalid.
 */
gboolean
sudoku_android_entry_new_game(const char *layout_name, int32_t seed)
{
    Map *map;

    (void)layout_name;

    if (s_state.main_ctx == NULL) {
        LOGE("entry:new_game: GLib not initialized");
        return FALSE;
    }

    if (s_state.game != NULL) {
        g_object_unref(s_state.game);
        s_state.game = NULL;
    }

    /* Fallback mode until real Sudokug exports are linked. */
    if (real_maps_load == NULL || real_game_new == NULL || real_game_generate == NULL) {
        s_state.selected_tile_0 = -1;
        s_state.selected_tile_1 = -1;
        s_state.move_count = 0;
        s_state.game_started = true;
        s_state.game_paused = false;
        s_state.fallback_mode = true;
        s_state.fallback_tile_count = 96;
        LOGI("entry:new_game: fallback mode enabled seed=0x%x", seed);
        dispatch_board_changed();
        return TRUE;
    }

    map = real_maps_load();
    if (map == NULL) {
        LOGE("entry:new_game: failed to load maps");
        return FALSE;
    }

    s_state.game = real_game_new(map);
    g_object_unref(map);

    if (s_state.game == NULL) {
        LOGE("entry:new_game: failed to create game");
        return FALSE;
    }

    real_game_generate(s_state.game, seed);

    /* Reset selection */
    s_state.selected_tile_0 = -1;
    s_state.selected_tile_1 = -1;
    s_state.move_count = 0;
    s_state.game_started = true;
    s_state.game_paused = false;
    s_state.fallback_mode = false;

    LOGI("entry:new_game: game started seed=0x%x", seed);
    dispatch_board_changed();
    return TRUE;
}

/**
 * sudoku_android_entry_get_tile_count:
 *
 * Returns: number of tiles in current game, or 0 if no game active.
 */
int
sudoku_android_entry_get_tile_count(void)
{
    if (s_state.fallback_mode) {
        return s_state.fallback_tile_count;
    }

    if (s_state.game == NULL || real_game_get_n_tiles == NULL) {
        return 0;
    }
    return real_game_get_n_tiles(s_state.game);
}

/**
 * sudoku_android_entry_set_paused:
 * @paused: pause state.
 *
 * Pause or resume game.
 */
void
sudoku_android_entry_set_paused(gboolean paused)
{
    if (s_state.game != NULL && real_game_set_paused != NULL) {
        real_game_set_paused(s_state.game, paused);
    }
    s_state.game_paused = paused;
    LOGI("entry:pause: paused=%d", paused);
    dispatch_board_changed();
}

/**
 * sudoku_android_entry_is_paused:
 *
 * Returns: true if game is paused.
 */
gboolean
sudoku_android_entry_is_paused(void)
{
    return s_state.game_paused;
}

/* ──────────────────────────────────────────────────────────── */
/* Touch Input API (Called from JNI)                             */
/* ──────────────────────────────────────────────────────────── */

/**
 * sudoku_android_entry_select_tile:
 * @tile_index: index of tile to select (0-based).
 *
 * Called when user touches a tile.
 * Manages two-tile selection for move.
 * Returns: true if move was made, false otherwise.
 */
gboolean
sudoku_android_entry_select_tile(int tile_index)
{
    Tile *t0, *t1;
    gboolean move_made;

    if (s_state.game == NULL && !s_state.fallback_mode) {
        LOGE("entry:select_tile: no game active");
        return FALSE;
    }

    if (tile_index < 0 || tile_index >= sudoku_android_entry_get_tile_count()) {
        LOGE("entry:select_tile: invalid index %d", tile_index);
        return FALSE;
    }

    if (s_state.game_paused) {
        LOGI("entry:select_tile: game paused, ignoring");
        return FALSE;
    }

    /* First tile selection */
    if (s_state.selected_tile_0 == -1) {
        s_state.selected_tile_0 = tile_index;
        LOGI("entry:select_tile: first tile %d selected", tile_index);
        dispatch_board_changed();
        return FALSE;  /* No move yet */
    }

    /* Same tile clicked again: deselect */
    if (s_state.selected_tile_0 == tile_index) {
        s_state.selected_tile_0 = -1;
        LOGI("entry:select_tile: tile %d deselected", tile_index);
        dispatch_board_changed();
        return FALSE;
    }

    /* Second tile selected: attempt move */
    s_state.selected_tile_1 = tile_index;
    LOGI("entry:select_tile: attempting move %d + %d", s_state.selected_tile_0, s_state.selected_tile_1);

    if (s_state.fallback_mode) {
        move_made = s_state.selected_tile_0 != s_state.selected_tile_1;
        if (move_made) {
            s_state.move_count++;
            LOGI("entry:select_tile: fallback move made total=%d", s_state.move_count);
        } else {
            LOGI("entry:select_tile: fallback move rejected");
        }
        s_state.selected_tile_0 = -1;
        s_state.selected_tile_1 = -1;
        dispatch_board_changed();
        return move_made;
    }

    if (real_game_get_tile == NULL || real_game_remove_pair == NULL) {
        LOGE("entry:select_tile: game methods not linked");
        s_state.selected_tile_0 = -1;
        s_state.selected_tile_1 = -1;
        dispatch_board_changed();
        return FALSE;
    }

    t0 = real_game_get_tile(s_state.game, s_state.selected_tile_0);
    t1 = real_game_get_tile(s_state.game, s_state.selected_tile_1);

    if (t0 == NULL || t1 == NULL) {
        LOGE("entry:select_tile: failed to get tile pointers");
        s_state.selected_tile_0 = -1;
        s_state.selected_tile_1 = -1;
        dispatch_board_changed();
        return FALSE;
    }

    move_made = real_game_remove_pair(s_state.game, t0, t1);

    if (move_made) {
        s_state.move_count++;
        LOGI("entry:select_tile: move made! total=%d", s_state.move_count);
    } else {
        LOGI("entry:select_tile: move rejected (invalid pair)");
    }

    s_state.selected_tile_0 = -1;
    s_state.selected_tile_1 = -1;
    dispatch_board_changed();

    return move_made;
}

/**
 * sudoku_android_entry_get_last_selection:
 * @out_tile_0: (out): first selected tile index (-1 if none).
 * @out_tile_1: (out): second selected tile index (-1 if none).
 *
 * Get current tile selection for UI rendering.
 */
void
sudoku_android_entry_get_last_selection(int *out_tile_0, int *out_tile_1)
{
    if (out_tile_0 != NULL)
        *out_tile_0 = s_state.selected_tile_0;
    if (out_tile_1 != NULL)
        *out_tile_1 = s_state.selected_tile_1;
}

unsigned int
sudoku_android_entry_get_move_count(void)
{
    return s_state.move_count;
}

void
sudoku_android_entry_get_grid_shape(int *out_cols, int *out_rows, int *out_tiles)
{
    int tile_count = sudoku_android_entry_get_tile_count();

    if (out_tiles != NULL) {
        *out_tiles = tile_count > 0 ? tile_count : 96;
    }

    compute_grid_shape(tile_count, out_cols, out_rows);
}

int
sudoku_android_entry_pick_tile_from_surface(float x, float y, int width, int height)
{
    int tile_count = sudoku_android_entry_get_tile_count();
    int cols = 0;
    int rows = 0;
    float margin_x;
    float margin_y;
    float board_w;
    float board_h;
    float cell_w;
    float cell_h;
    int best_idx = -1;
    float best_dist2 = FLT_MAX;
    int row;
    int col;

    if (width <= 0 || height <= 0) {
        return -1;
    }

    if (x < 0.0f || y < 0.0f || x >= (float)width || y >= (float)height) {
        return -1;
    }

    compute_grid_shape(tile_count, &cols, &rows);
    if (cols <= 0 || rows <= 0) {
        return -1;
    }

    margin_x = (float)width * 0.06f;
    margin_y = (float)height * 0.08f;
    board_w = (float)width - (2.0f * margin_x);
    board_h = (float)height - (2.0f * margin_y);
    if (board_w <= 1.0f || board_h <= 1.0f) {
        return -1;
    }

    cell_w = board_w / (float)cols;
    cell_h = board_h / (float)rows;

    for (row = 0; row < rows; row++) {
        float stagger = (row % 2 == 0) ? 0.0f : (cell_w * 0.5f);

        for (col = 0; col < cols; col++) {
            int idx = row * cols + col;
            float cx;
            float cy;
            float dx;
            float dy;
            float d2;

            if (idx >= tile_count) {
                continue;
            }

            cx = margin_x + stagger + ((float)col + 0.5f) * cell_w;
            cy = margin_y + ((float)row + 0.5f) * cell_h;
            dx = x - cx;
            dy = y - cy;
            d2 = dx * dx + dy * dy;

            if (d2 < best_dist2) {
                best_dist2 = d2;
                best_idx = idx;
            }
        }
    }

    if (best_idx < 0) {
        return -1;
    }

    /* Ignore taps far away from any tile center. */
    {
        float max_r = (cell_w < cell_h ? cell_w : cell_h) * 0.9f;
        if (best_dist2 > (max_r * max_r)) {
            return -1;
        }
    }

    return best_idx;
}

/* ──────────────────────────────────────────────────────────── */
/* Signal Bridge (Tile Rendering Notifications)                 */
/* ──────────────────────────────────────────────────────────── */

/**
 * sudoku_android_entry_connect_on_moved:
 * @callback: function to call when board changes.
 * @user_data: (nullable): opaque user context passed to callback.
 *
 * Connect callback to board change notifications.
 * Callback signature: void (*callback)(void *user_data)
 */
void
sudoku_android_entry_connect_on_moved(SudokuMovedCallback callback, void *user_data)
{
    s_on_moved_cb = callback;
    s_on_moved_user_data = user_data;

    if (callback == NULL) {
        LOGI("entry:connect_on_moved: disconnected");
        return;
    }

    LOGI("entry:connect_on_moved: connected");
}
