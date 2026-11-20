#pragma once
#include <pebble.h>

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define HAS_TOUCH 1
#else
#define HAS_TOUCH 0
#endif

typedef enum {
  GESTURE_TAP,
  GESTURE_SWIPE_LEFT,
  GESTURE_SWIPE_RIGHT,
  // Drag-scroll: fired during PositionUpdate when the finger crosses
  // ROW_STEP_PX vertically. Naming matches the user's intent — "scroll
  // up the list" reveals later rows (selection moves down), "scroll
  // down" reveals earlier rows (selection moves up). Phone convention.
  GESTURE_SCROLL_UP,
  GESTURE_SCROLL_DOWN,
} GestureKind;

typedef void (*GestureHandler)(GestureKind kind, void *context);

void touch_input_init(void);
void touch_input_deinit(void);

// Per-window: install in window_load, clear in window_unload.
// Dispatch finds the topmost window via window_stack_get_top_window().
void touch_input_set_handler(Window *win, GestureHandler handler, void *context);
void touch_input_clear_handler(Window *win);

// Tapping a row. The menus give every row this height, so the row under a
// finger can be worked out from where the tap landed.
#define TOUCH_ROW_H 61  // the Time 2's own row height for apps
#if HAS_TOUCH
int16_t touch_input_row_height(MenuLayer *menu, MenuIndex *index, void *context);
#define TOUCH_ROW_HEIGHT_CALLBACK .get_cell_height = touch_input_row_height,
#else
#define TOUCH_ROW_HEIGHT_CALLBACK
#endif
// The row the last tap landed on, in a one-section menu with `header_h`
// pixels of header and `num_rows` rows. False when it missed every row.
bool touch_input_tap_row(MenuLayer *menu, int16_t header_h, uint16_t num_rows, MenuIndex *out);
