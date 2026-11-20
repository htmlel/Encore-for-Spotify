#include <pebble.h>
#include "notice.h"

#define NOTICE_MS 3500

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;
static char s_text[96];
static bool s_error;

static void dismiss(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_window) {
    window_stack_remove(s_window, true);
  }
}

static void timer_cb(void *data) {
  s_timer = NULL;
  dismiss();
}

static void click_cb(ClickRecognizerRef recognizer, void *context) {
  dismiss();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, click_cb);
  window_single_click_subscribe(BUTTON_ID_UP, click_cb);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_cb);
}

static void draw(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  // A colored band across the top says what kind of message it is.
  graphics_context_set_fill_color(ctx, s_error ? PBL_IF_COLOR_ELSE(GColorSunsetOrange, GColorWhite)
                                               : PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite));
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, 8), 0, GCornerNone);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const GRect box = GRect(12, 0, bounds.size.w - 24, bounds.size.h);
  const GSize size = graphics_text_layout_get_content_size(s_text, font, box,
                                                           GTextOverflowModeWordWrap,
                                                           GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_text, font,
                     GRect(box.origin.x, (bounds.size.h - size.h) / 2 - 4, box.size.w, size.h + 8),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, draw);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
  s_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void notice_show(const char *text, bool is_error) {
  strncpy(s_text, text, sizeof(s_text) - 1);
  s_text[sizeof(s_text) - 1] = '\0';
  s_error = is_error;
#if !defined(DEMO_NOTICE)  // the sample notice stays up for its picture
  if (s_timer) {
    app_timer_reschedule(s_timer, NOTICE_MS);
  } else {
    s_timer = app_timer_register(NOTICE_MS, timer_cb, NULL);
  }
#endif
  if (s_window) {
    // Already up: show the newer message.
    if (s_layer) layer_mark_dirty(s_layer);
    return;
  }
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  if (is_error) {
    vibes_short_pulse();
  }
}
