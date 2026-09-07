/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef GDK_ANDROID_INPUT_H
#define GDK_ANDROID_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	GDK_ANDROID_TOUCH_PHASE_NONE = 0,
	GDK_ANDROID_TOUCH_PHASE_DOWN = 1,
	GDK_ANDROID_TOUCH_PHASE_MOVE = 2,
	GDK_ANDROID_TOUCH_PHASE_UP = 3,
	GDK_ANDROID_TOUCH_PHASE_CANCEL = 4,
} GdkAndroidTouchPhase;

typedef struct {
	GdkAndroidTouchPhase phase;
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
} GdkAndroidTouchEvent;

void gdk_android_input_init(void);
void gdk_android_input_ime_show(void);
void gdk_android_input_ime_hide(void);
void gdk_android_input_cancel_active_touches(const char *reason);
void gdk_android_input_motion(int action,
							  int action_index,
							  int pointer_index,
							  int pointer_id,
							  float x,
							  float y,
								  int pointer_count,
								  uint64_t event_time_ms);
void gdk_android_input_commit_text(const char *text);
void gdk_android_input_copy_last_commit_text(char *buffer, size_t buffer_size);
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
										bool *valid);
bool gdk_android_input_pop_touch_event(GdkAndroidTouchEvent *event);
void gdk_android_input_copy_touch_stream_stats(unsigned int *dropped,
									 unsigned int *filtered,
									 unsigned int *normalized,
									 unsigned int *lifecycle_forced_cancel);

#ifdef __cplusplus
}
#endif

#endif
