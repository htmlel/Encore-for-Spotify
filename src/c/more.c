#include <pebble.h>
#include "more.h"

#if !defined(PBL_PLATFORM_APLITE)

#include "comm.h"
#include "playback_data.h"
#include "touch_input.h"
#include "list.h"

// Initial state is read from main.c when the window loads; subsequent
// taps toggle a local copy optimistically so the submenu re-renders
// immediately without waiting for the next Spotify poll to confirm.
extern bool main_get_shuffle(void);
extern int  main_get_repeat(void);

static Window *s_window;
static MenuLayer *s_menu;
static bool s_shuffle;
static int  s_repeat;
static int  s_liked;  // 0 no, 1 yes, 2 not known

static const char *shuffle_subtitle(void) {
  return s_shuffle ? "On" : "Off";
}

static const char *repeat_subtitle(void) {
  switch (s_repeat) {
    case 1: return "Context";
    case 2: return "Track";
    default: return "Off";
  }
}

// Rows: like, devices, shuffle, repeat.
static uint16_t get_num_rows(MenuLayer *m, uint16_t s, void *d) { return 4; }

static const char *like_subtitle(void) {
  switch (s_liked) {
    case 0: return "Not in Liked Songs";
    case 1: return "In Liked Songs";
    default: return NULL;
  }
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *d) {
  switch (idx->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell, s_liked == 1 ? "Unlike Song" : "Like Song",
                           like_subtitle(), NULL);
      break;
    case 1:
      menu_cell_basic_draw(ctx, cell, "Play On...", "Choose a speaker or device", NULL);
      break;
    case 2:
      menu_cell_basic_draw(ctx, cell, "Shuffle", shuffle_subtitle(), NULL);
      break;
    default:
      menu_cell_basic_draw(ctx, cell, "Repeat", repeat_subtitle(), NULL);
      break;
  }
}

static void select_click(MenuLayer *m, MenuIndex *idx, void *d) {
  switch (idx->row) {
    case 0:
      // Shown at once; the phone confirms with a status message.
      s_liked = s_liked == 1 ? 0 : 1;
      comm_send_command(CMD_TOGGLE_LIKE, NULL);
      break;
    case 1:
      list_window_push(LIST_TYPE_DEVICES);
      comm_send_command(CMD_FETCH_DEVICES, NULL);
      return;
    case 2:
      s_shuffle = !s_shuffle;
      comm_send_command(CMD_TOGGLE_SHUFFLE, NULL);
      break;
    default:
      s_repeat = (s_repeat + 1) % 3;
      comm_send_command(CMD_CYCLE_REPEAT, NULL);
      break;
  }
  menu_layer_reload_data(s_menu);
}

#if HAS_TOUCH
static void more_gesture(GestureKind kind, void *ctx) {
  if (!s_menu) return;
  switch (kind) {
    case GESTURE_TAP: {
      // The row under the finger; a tap that misses them all acts on the
      // highlighted row, as the Select button does.
      MenuIndex idx;
      if (touch_input_tap_row(s_menu, 0, 4, &idx)) {
        menu_layer_set_selected_index(s_menu, idx, MenuRowAlignNone, false);
      } else {
        idx = menu_layer_get_selected_index(s_menu);
      }
      select_click(s_menu, &idx, NULL);
      break;
    }
    case GESTURE_SCROLL_UP:
      // No slide: under a moving finger the slides queue up and trail behind.
      // Finger moved UP — push content up — selection advances DOWN.
      menu_layer_set_selected_next(s_menu, false, MenuRowAlignCenter, false);
      break;
    case GESTURE_SCROLL_DOWN:
      // No slide: under a moving finger the slides queue up and trail behind.
      // Finger moved DOWN — pull content down — selection retreats UP.
      menu_layer_set_selected_next(s_menu, true, MenuRowAlignCenter, false);
      break;
    case GESTURE_SWIPE_LEFT:
      window_stack_pop(true);
      break;
    case GESTURE_SWIPE_RIGHT:
      // Ignored.
      break;
    case GESTURE_HOLD:
      break;
  }
}
#endif

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_shuffle = main_get_shuffle();
  s_repeat  = main_get_repeat();
  s_liked   = comm_get_liked();

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = get_num_rows,
    TOUCH_ROW_HEIGHT_CALLBACK
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu,
    PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite),
    PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
#if HAS_TOUCH
  touch_input_set_handler(window, more_gesture, NULL);
#endif
}

static void window_unload(Window *window) {
#if HAS_TOUCH
  touch_input_clear_handler(window);
#endif
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void more_window_push(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

#else  // PBL_PLATFORM_APLITE — submenu not available, but keep symbol for linker
void more_window_push(void) { /* no-op on aplite */ }
#endif
