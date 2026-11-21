// Now Playing for the Pebble Time 2.
//
// A music-player card on a black background with green accents: the cover on
// the left with the title, artist and playing device beside it; a row of pill
// buttons (previous, play/pause, next); and along the bottom a shuffle button
// and a progress slider with time gone and time left. The time and a heart
// (filled when the song is in Liked Songs) sit in the top corners.
//
// One of the two Now Playing looks on the Time 2 (see ui_time2.c); the other
// is ui_cover.c.

#include <pebble.h>
#include "ui.h"
#include "comm.h"
#include "ui_looks.h"
#include "heart.h"
#include "marquee.h"
#include "mode_button.h"

#if defined(PBL_PLATFORM_EMERY)

#define SCREEN_W 200

#define COVER_X 8
#define COVER_Y 34
#define COVER_SIZE 108      // the cover arrives as a square this size
#define COVER_RADIUS 8

#define TEXT_X 124
#define TEXT_W 70

#define BUTTON_Y 149
#define BUTTON_H 28
#define SLIDER_Y 200
#define SLIDER_X0 62
#define SLIDER_X1 190

#define HEART_CX 172   // in from the screen's rounded corner
#define HEART_CY 14

#define COLOR_ACCENT GColorJaegerGreen
#define COLOR_PILL GColorDarkGray
#define COLOR_DIM GColorLightGray

static Layer *s_canvas;
static MarqueeLayer *s_title_layer;   // the song name; scrolls when it is too long to fit

static GBitmap *s_art;          // owned by comm.c
static char s_title[64] = "Encore";
static char s_artist[64] = "Nothing playing";
static char s_clock[8];
static int32_t s_elapsed;
static int32_t s_total;
static bool s_playing;
static bool s_shuffle;
static int s_repeat;

// The volume just set, shown for a moment where the device name is. A notice
// would get in the way of pressing the button again.
#define VOLUME_SHOW_MS 2500
static char s_volume_text[20];
static AppTimer *s_volume_timer;

// --- Drawing ----------------------------------------------------------------

static void fill_pill(GContext *ctx, GRect r, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, r, r.size.h / 2, GCornersAll);
}

static void draw_triangle(GContext *ctx, int x, int y, int w, int h, bool right) {
  GPathInfo info = {
      .num_points = 3,
      .points = right ? (GPoint[]){{0, 0}, {w, h / 2}, {0, h}}
                      : (GPoint[]){{w, 0}, {0, h / 2}, {w, h}},
  };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_move_to(path, GPoint(x, y));
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

static void draw_cover(GContext *ctx) {
  const GRect frame = GRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE);
  if (s_art) {
    const GRect b = gbitmap_get_bounds(s_art);
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_draw_bitmap_in_rect(ctx, s_art,
                                 GRect(frame.origin.x + (frame.size.w - b.size.w) / 2,
                                       frame.origin.y + (frame.size.h - b.size.h) / 2, b.size.w,
                                       b.size.h));
    // Round the corners by painting the background back over them.
    graphics_context_set_stroke_color(ctx, GColorBlack);
    const int r = COVER_RADIUS;
    for (int dy = 0; dy < r; dy++) {
      for (int dx = 0; dx < r; dx++) {
        if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) {
          const int x0 = frame.origin.x;
          const int y0 = frame.origin.y;
          const int x1 = x0 + frame.size.w - 1;
          const int y1 = y0 + frame.size.h - 1;
          graphics_draw_pixel(ctx, GPoint(x0 + dx, y0 + dy));
          graphics_draw_pixel(ctx, GPoint(x1 - dx, y0 + dy));
          graphics_draw_pixel(ctx, GPoint(x0 + dx, y1 - dy));
          graphics_draw_pixel(ctx, GPoint(x1 - dx, y1 - dy));
        }
      }
    }
    return;
  }
  // No cover yet: a plain sleeve with a note on it.
  graphics_context_set_fill_color(ctx, COLOR_PILL);
  graphics_fill_rect(ctx, frame, COVER_RADIUS, GCornersAll);
  const int cx = frame.origin.x + frame.size.w / 2;
  const int cy = frame.origin.y + frame.size.h / 2;
  graphics_context_set_fill_color(ctx, COLOR_DIM);
  graphics_fill_circle(ctx, GPoint(cx - 7, cy + 14), 9);
  graphics_fill_rect(ctx, GRect(cx - 2, cy - 22, 5, 38), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx - 2, cy - 22, 20, 7), 0, GCornerNone);
}


static void draw_top(GContext *ctx) {
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_clock, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(12, 2, 70, 22), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  // The heart: green when the song is liked, a white outline when not.
  const bool liked = comm_get_liked() == 1;
  heart_draw(ctx, GPoint(HEART_CX, HEART_CY), 10, 2, liked ? COLOR_ACCENT : GColorWhite, false,
             liked, COLOR_ACCENT);
}

static void draw_text(GContext *ctx) {
  // The song name is its own scrolling layer, above this one.
  graphics_context_set_text_color(ctx, GColorWhite);
  // The artist may take two lines; the device goes right under it.
  GFont artist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  const GRect artist_box = GRect(TEXT_X, COVER_Y + 20, TEXT_W, 42);
  const GSize artist_size = graphics_text_layout_get_content_size(
      s_artist, artist_font, artist_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  graphics_draw_text(ctx, s_artist, artist_font, artist_box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  const char *device = comm_get_device_name();
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  const int line_y = artist_box.origin.y + (artist_size.h > 24 ? 42 : 22);
  const GRect line = GRect(TEXT_X, line_y, TEXT_W, COVER_Y + COVER_SIZE + 4 - line_y);
  if (s_volume_text[0]) {
    graphics_context_set_text_color(ctx, COLOR_ACCENT);
    graphics_draw_text(ctx, s_volume_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), line,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else if (device && device[0]) {
    char text[48];
    snprintf(text, sizeof(text), "on %s", device);
    graphics_context_set_text_color(ctx, COLOR_DIM);
    graphics_draw_text(ctx, text, small, line, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
  }
}

static void draw_buttons(GContext *ctx) {
  const int cy = BUTTON_Y + BUTTON_H / 2;
  // Previous.
  fill_pill(ctx, GRect(8, BUTTON_Y, 54, BUTTON_H), COLOR_PILL);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(23, cy - 7, 3, 14), 0, GCornerNone);
  draw_triangle(ctx, 27, cy - 7, 10, 14, false);
  draw_triangle(ctx, 36, cy - 7, 10, 14, false);
  // Play or pause: the one green button.
  fill_pill(ctx, GRect(68, BUTTON_Y, 64, BUTTON_H), COLOR_ACCENT);
  graphics_context_set_fill_color(ctx, GColorBlack);
  if (s_playing) {
    graphics_fill_rect(ctx, GRect(92, cy - 8, 5, 16), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(103, cy - 8, 5, 16), 0, GCornerNone);
  } else {
    draw_triangle(ctx, 94, cy - 8, 15, 16, true);
  }
  // Next.
  fill_pill(ctx, GRect(138, BUTTON_Y, 54, BUTTON_H), COLOR_PILL);
  graphics_context_set_fill_color(ctx, GColorWhite);
  draw_triangle(ctx, 154, cy - 7, 10, 14, true);
  draw_triangle(ctx, 163, cy - 7, 10, 14, true);
  graphics_fill_rect(ctx, GRect(174, cy - 7, 3, 14), 0, GCornerNone);
}

// The button at the bottom left shows shuffle or repeat; holding it flips
// it over to the other one (see mode_button.h).
static GRect mode_button_rect(void) {
  return GRect(8, SLIDER_Y - 14, 44, 28);
}

static void draw_mode_button(GContext *ctx) {
  const GRect pill = mode_button_rect();
  fill_pill(ctx, pill, COLOR_PILL);
  mode_button_draw(ctx, GPoint(pill.origin.x + pill.size.w / 2, pill.origin.y + pill.size.h / 2),
                   s_shuffle, s_repeat, COLOR_ACCENT, GPoint(18, -11), false);
}

static void format_time(char *out, size_t size, int32_t sec, bool minus) {
  if (sec < 0) {
    sec = 0;
  }
  snprintf(out, size, "%s%d:%02d", minus ? "-" : "", (int)(sec / 60), (int)(sec % 60));
}

static void draw_slider(GContext *ctx) {
  const int width = SLIDER_X1 - SLIDER_X0;
  graphics_context_set_fill_color(ctx, COLOR_PILL);
  graphics_fill_rect(ctx, GRect(SLIDER_X0, SLIDER_Y - 2, width, 4), 2, GCornersAll);
  int at = 0;
  if (s_total > 0) {
    at = (int)((int64_t)width * s_elapsed / s_total);
    if (at > width) {
      at = width;
    }
  }
  graphics_context_set_fill_color(ctx, COLOR_ACCENT);
  graphics_fill_rect(ctx, GRect(SLIDER_X0, SLIDER_Y - 2, at, 4), 2, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(SLIDER_X0 + at, SLIDER_Y), 6);

  if (s_total > 0) {
    char gone[16];
    char left[16];
    format_time(gone, sizeof(gone), s_elapsed, false);
    format_time(left, sizeof(left), s_total - s_elapsed, true);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, COLOR_DIM);
    graphics_draw_text(ctx, gone, font, GRect(SLIDER_X0, SLIDER_Y + 5, 50, 16),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, left, font, GRect(SLIDER_X1 - 50, SLIDER_Y + 5, 50, 16),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  draw_top(ctx);
  draw_cover(ctx);
  draw_text(ctx);
  draw_buttons(ctx);
  draw_mode_button(ctx);
  draw_slider(ctx);
}

static void redraw(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

bool card_hold(GPoint at) {
  const GRect pill = mode_button_rect();
  // A little room around the button for a fingertip.
  if (at.x > pill.origin.x + pill.size.w + 8 || at.y < pill.origin.y - 10) {
    return false;
  }
  mode_button_flip(redraw);
  return true;
}

// --- Clock ------------------------------------------------------------------

static void set_clock(struct tm *t) {
  strftime(s_clock, sizeof(s_clock), clock_is_24h_style() ? "%H:%M" : "%l:%M", t);
  // "%l" pads single-digit hours with a space; drop it.
  if (s_clock[0] == ' ') {
    memmove(s_clock, s_clock + 1, strlen(s_clock));
  }
#if defined(DEMO_NP)
  // The same time in every simulator picture, so a GIF made of them does
  // not jump about.
  strcpy(s_clock, "9:41");
#endif
}

static void clock_tick(struct tm *t, TimeUnits changed) {
  set_clock(t);
  redraw();
}

// --- The ui.h interface -----------------------------------------------------

void card_init(Window *window) {
  Layer *root = window_get_root_layer(window);
  // The cover is handed over again by the caller once the screen is up.
  s_art = NULL;

  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_title_layer = marquee_layer_create(GRect(TEXT_X, COVER_Y - 4, SCREEN_W - TEXT_X - 4, 24));
  if (s_title_layer) {
    marquee_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    marquee_layer_set_text_color(s_title_layer, GColorWhite);
    marquee_layer_set_alignment(s_title_layer, GTextAlignmentLeft);
    marquee_layer_set_text(s_title_layer, s_title);
    layer_add_child(root, marquee_layer_get_layer(s_title_layer));
  }

  time_t now = time(NULL);
  set_clock(localtime(&now));
  tick_timer_service_subscribe(MINUTE_UNIT, clock_tick);
}

void card_deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_volume_timer) {
    app_timer_cancel(s_volume_timer);
    s_volume_timer = NULL;
  }
  s_volume_text[0] = '\0';
  mode_button_stop();
  if (s_title_layer) {
    marquee_layer_destroy(s_title_layer);
    s_title_layer = NULL;
  }
  if (s_canvas) {
    layer_destroy(s_canvas);
    s_canvas = NULL;
  }
  s_art = NULL;
}

void card_start_loading(void) {
  // Whatever is known shows at once; the cover arrives with the first report.
}

void card_set_album_art(GBitmap *bitmap) {
  s_art = bitmap;
  // The previous cover is no longer drawn, so comm.c may free it.
  comm_release_old_art();
  redraw();
}

void card_clear_album_art_instant(void) {
  s_art = NULL;
  redraw();
}

static void volume_hide(void *data) {
  s_volume_timer = NULL;
  s_volume_text[0] = '\0';
  redraw();
}

void card_set_status(const char *text) {
  // Only the volume is shown here; other messages worth showing come up as
  // notices (see notice.c).
  if (strncmp(text, "Volume ", 7) != 0 || !strchr(text, '%')) {
    return;
  }
  strncpy(s_volume_text, text, sizeof(s_volume_text) - 1);
  s_volume_text[sizeof(s_volume_text) - 1] = '\0';
  if (s_volume_timer) {
    app_timer_reschedule(s_volume_timer, VOLUME_SHOW_MS);
  } else {
    s_volume_timer = app_timer_register(VOLUME_SHOW_MS, volume_hide, NULL);
  }
  redraw();
}

void card_set_track_info(const char *title, const char *artist) {
  if (strcmp(s_title, title) != 0) {
    strncpy(s_title, title, sizeof(s_title) - 1);
    s_title[sizeof(s_title) - 1] = '\0';
    if (s_title_layer) {
      marquee_layer_set_text(s_title_layer, s_title);
    }
  }
  strncpy(s_artist, artist, sizeof(s_artist) - 1);
  s_artist[sizeof(s_artist) - 1] = '\0';
  redraw();
}

void card_set_progress(int elapsed_sec, int total_sec) {
  s_elapsed = elapsed_sec;
  s_total = total_sec;
  redraw();
}

void card_set_shuffle(bool on) {
  s_shuffle = on;
  redraw();
}

void card_set_repeat(int state) {
  s_repeat = state;
  redraw();
}

void card_hint_animation_dir(bool reverse) {
  (void)reverse;  // this screen has no slide-in animation
}

void card_set_playing(bool playing) {
  s_playing = playing;
  redraw();
}

void card_refresh(void) {
  redraw();
}

// Everything is always on screen here.
void card_wake(void) {}
bool card_controls_shown(void) { return true; }

UiHit card_hit(GPoint p) {
  // The heart, with room around it for a fingertip.
  if (p.x >= SCREEN_W - 56 && p.y < COVER_Y) {
    return UI_HIT_HEART;
  }
  // The cover plays and pauses.
  if (p.x < TEXT_X && p.y >= COVER_Y && p.y < COVER_Y + COVER_SIZE) {
    return UI_HIT_RECORD;
  }
  if (p.y >= BUTTON_Y - 6 && p.y < BUTTON_Y + BUTTON_H + 6) {
    return p.x < 65 ? UI_HIT_PREV : p.x < 135 ? UI_HIT_PLAY_PAUSE : UI_HIT_NEXT;
  }
  if (p.y >= SLIDER_Y - 20 && p.x < 58) {
    return mode_button_is_repeat() ? UI_HIT_REPEAT : UI_HIT_SHUFFLE;
  }
  return UI_HIT_NONE;
}

#endif  // PBL_PLATFORM_EMERY
