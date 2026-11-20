// Now Playing for the Pebble Time 2.
//
// The album cover up top, on a background tinted with the cover's own color.
// Under it: title, artist, the device the music is on, a progress bar with
// time gone and time left, and touch buttons. A heart beside the cover shows
// whether the song is in Liked Songs.
//
// This file replaces ui.c on the Time 2 (ui.c is the original screen, still
// used for the other watches if the app is ever built for them).

#include <pebble.h>
#include "ui.h"
#include "comm.h"
#include "marquee.h"

#if defined(PBL_PLATFORM_EMERY)

#define COVER_SIZE 124      // the cover arrives as a square this size
#define COVER_X 38
#define COVER_Y 3

#define TITLE_Y 126
#define ARTIST_Y 153
#define DEVICE_Y 172
#define PROGRESS_Y 198
#define BUTTONS_Y 190       // touch buttons take the strip from here down
#define BUTTON_ICON_Y 216

static Layer *s_canvas;
static MarqueeLayer *s_title_layer;

static GBitmap *s_art;          // owned by comm.c
static GColor s_bg = {GColorOxfordBlueARGB8};
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

// --- Colors -----------------------------------------------------------------

// A dark tint of the cover for the background, so every album gets its own
// look and the white text stays readable.
static GColor background_for(GBitmap *bmp) {
  if (!bmp || gbitmap_get_format(bmp) != GBitmapFormat8Bit) {
    return GColorOxfordBlue;
  }
  const GRect b = gbitmap_get_bounds(bmp);
  const uint8_t *data = gbitmap_get_data(bmp);
  const int stride = gbitmap_get_bytes_per_row(bmp);
  int r = 0, g = 0, bl = 0, n = 0;
  for (int y = 0; y < b.size.h; y += 3) {
    for (int x = 0; x < b.size.w; x += 3) {
      const uint8_t px = data[y * stride + x];
      r += (px >> 4) & 3;
      g += (px >> 2) & 3;
      bl += px & 3;
      n++;
    }
  }
  if (n == 0) {
    return GColorOxfordBlue;
  }
  // Average 0..3 per channel, scaled to 0..2 so it stays on the dark side.
  int cr = (r * 2 + n) / (n * 3);
  int cg = (g * 2 + n) / (n * 3);
  int cb = (bl * 2 + n) / (n * 3);
  if (cr > 2) cr = 2;
  if (cg > 2) cg = 2;
  if (cb > 2) cb = 2;
  if (cr + cg + cb == 0) {
    return GColorDarkGray;   // a black cover still gets a backdrop to sit on
  }
  return (GColor){.argb = (uint8_t)(0xC0 | (cr << 4) | (cg << 2) | cb)};
}

// --- Drawing ----------------------------------------------------------------

static void draw_cover(GContext *ctx) {
  const GRect frame = GRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE);
  // A black edge lifts the cover off the background.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, grect_inset(frame, GEdgeInsets(-2)), 0, GCornerNone);
  if (s_art) {
    const GRect b = gbitmap_get_bounds(s_art);
    // Centered, in case a cover of another size is still on its way out.
    const GRect dest = GRect(frame.origin.x + (frame.size.w - b.size.w) / 2,
                             frame.origin.y + (frame.size.h - b.size.h) / 2, b.size.w, b.size.h);
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_draw_bitmap_in_rect(ctx, s_art, grect_crop(dest, 0));
    return;
  }
  // No cover yet: a plain sleeve with a note on it.
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, frame, 0, GCornerNone);
  const int cx = frame.origin.x + frame.size.w / 2;
  const int cy = frame.origin.y + frame.size.h / 2;
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_circle(ctx, GPoint(cx - 8, cy + 16), 10);
  graphics_fill_rect(ctx, GRect(cx - 2, cy - 26, 5, 44), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx - 2, cy - 26, 22, 8), 0, GCornerNone);
}

// A heart from two circles and a triangle, centered on (cx, cy).
static void draw_heart_shape(GContext *ctx, int cx, int cy, int grow) {
  const int r = 5 + grow;
  graphics_fill_circle(ctx, GPoint(cx - 5, cy - 3), r);
  graphics_fill_circle(ctx, GPoint(cx + 5, cy - 3), r);
  // Close the gap the two circles leave between them.
  graphics_fill_rect(ctx, GRect(cx - 3, cy - 4, 7, 6), 0, GCornerNone);
  GPathInfo info = {
      .num_points = 3,
      .points = (GPoint[]){{-10 - grow, 0}, {10 + grow, 0}, {0, 11 + grow}},
  };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_move_to(path, GPoint(cx, cy));
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

static void draw_heart(GContext *ctx) {
  const int cx = 18;
  const int cy = 108;
  const bool liked = comm_get_liked() == 1;
  // An outline: the shape a size up in white, then the middle filled in red
  // when liked or with the background when not.
  graphics_context_set_fill_color(ctx, GColorWhite);
  draw_heart_shape(ctx, cx, cy, 2);
  graphics_context_set_fill_color(ctx, liked ? GColorRed : s_bg);
  draw_heart_shape(ctx, cx, cy, 0);
}

static void draw_side_labels(GContext *ctx) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_context_set_text_color(ctx, GColorWhite);
  // The time, in the strip left of the cover.
  graphics_draw_text(ctx, s_clock, font, GRect(0, 0, COVER_X - 2, 18), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  // Shuffle and repeat, in the strip on the right, only when they are on.
  const GRect right = GRect(COVER_X + COVER_SIZE + 2, 0, 200 - COVER_X - COVER_SIZE - 2, 18);
  if (s_shuffle) {
    graphics_draw_text(ctx, "SHUF", font, right, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  if (s_repeat) {
    graphics_draw_text(ctx, s_repeat == 2 ? "RPT1" : "RPT", font,
                       GRect(right.origin.x, 18, right.size.w, 18), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
  }
}

static void format_time(char *out, size_t size, int32_t sec, bool minus) {
  if (sec < 0) {
    sec = 0;
  }
  snprintf(out, size, "%s%d:%02d", minus ? "-" : "", (int)(sec / 60), (int)(sec % 60));
}

static void draw_text_and_progress(GContext *ctx) {
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_artist, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(8, ARTIST_Y, 184, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  const char *device = comm_get_device_name();
  if (s_volume_text[0]) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_volume_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(8, DEVICE_Y, 184, 18), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else if (device && device[0]) {
    char line[48];
    snprintf(line, sizeof(line), "on %s", device);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(8, DEVICE_Y, 184, 18), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

  // Time gone, a bar, time left.
  if (s_total > 0) {
    char gone[16];
    char left[16];
    format_time(gone, sizeof(gone), s_elapsed, false);
    format_time(left, sizeof(left), s_total - s_elapsed, true);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, gone, font, GRect(2, PROGRESS_Y - 10, 40, 16), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, left, font, GRect(158, PROGRESS_Y - 10, 40, 16), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    const GRect bar = GRect(44, PROGRESS_Y, 112, 4);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bar, 2, GCornersAll);
    const int w = (int)((int64_t)bar.size.w * s_elapsed / s_total);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(bar.origin.x, bar.origin.y, w > bar.size.w ? bar.size.w : w, 4), 2,
                       GCornersAll);
  }
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

static void draw_buttons(GContext *ctx) {
  const int y = BUTTON_ICON_Y - 7;
  graphics_context_set_fill_color(ctx, GColorWhite);
  // Previous: a bar and two triangles pointing left.
  graphics_fill_rect(ctx, GRect(20, y, 3, 14), 0, GCornerNone);
  draw_triangle(ctx, 24, y, 10, 14, false);
  draw_triangle(ctx, 34, y, 10, 14, false);
  // Play or pause.
  if (s_playing) {
    graphics_fill_rect(ctx, GRect(92, y - 1, 5, 16), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(103, y - 1, 5, 16), 0, GCornerNone);
  } else {
    draw_triangle(ctx, 93, y - 1, 15, 16, true);
  }
  // Next: two triangles pointing right and a bar.
  draw_triangle(ctx, 156, y, 10, 14, true);
  draw_triangle(ctx, 166, y, 10, 14, true);
  graphics_fill_rect(ctx, GRect(177, y, 3, 14), 0, GCornerNone);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  draw_cover(ctx);
  draw_heart(ctx);
  draw_side_labels(ctx);
  draw_text_and_progress(ctx);
  draw_buttons(ctx);
}

static void redraw(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

// --- Clock ------------------------------------------------------------------

static void set_clock(struct tm *t) {
  strftime(s_clock, sizeof(s_clock), clock_is_24h_style() ? "%H:%M" : "%l:%M", t);
  // "%l" pads single-digit hours with a space; drop it so the time centers.
  if (s_clock[0] == ' ') {
    memmove(s_clock, s_clock + 1, strlen(s_clock));
  }
}

static void clock_tick(struct tm *t, TimeUnits changed) {
  set_clock(t);
  redraw();
}

// --- The ui.h interface -----------------------------------------------------

void ui_init(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  // The cover is handed over again by the caller once the screen is up.
  s_art = NULL;
  s_bg = GColorOxfordBlue;

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_title_layer = marquee_layer_create(GRect(8, TITLE_Y, bounds.size.w - 16, 30));
  if (s_title_layer) {
    marquee_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    marquee_layer_set_text_color(s_title_layer, GColorWhite);
    marquee_layer_set_alignment(s_title_layer, GTextAlignmentCenter);
    marquee_layer_set_text(s_title_layer, s_title);
    layer_add_child(root, marquee_layer_get_layer(s_title_layer));
  }

  time_t now = time(NULL);
  set_clock(localtime(&now));
  tick_timer_service_subscribe(MINUTE_UNIT, clock_tick);
}

void ui_deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_volume_timer) {
    app_timer_cancel(s_volume_timer);
    s_volume_timer = NULL;
  }
  s_volume_text[0] = '\0';
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

void ui_start_loading(void) {
  // Whatever is known shows at once; the cover arrives with the first report.
}

void ui_set_album_art(GBitmap *bitmap) {
  s_art = bitmap;
  s_bg = background_for(bitmap);
  // The previous cover is no longer drawn, so comm.c may free it.
  comm_release_old_art();
  redraw();
}

void ui_clear_album_art_instant(void) {
  s_art = NULL;
  s_bg = GColorOxfordBlue;
  redraw();
}

static void volume_hide(void *data) {
  s_volume_timer = NULL;
  s_volume_text[0] = '\0';
  redraw();
}

void ui_set_status(const char *text) {
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

void ui_set_track_info(const char *title, const char *artist) {
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

void ui_set_progress(int elapsed_sec, int total_sec) {
  s_elapsed = elapsed_sec;
  s_total = total_sec;
  redraw();
}

void ui_set_shuffle(bool on) {
  s_shuffle = on;
  redraw();
}

void ui_set_repeat(int state) {
  s_repeat = state;
  redraw();
}

void ui_hint_animation_dir(bool reverse) {
  (void)reverse;  // this screen has no slide-in animation
}

void ui_set_playing(bool playing) {
  s_playing = playing;
  redraw();
}

void ui_refresh(void) {
  redraw();
}

UiHit ui_hit(GPoint p) {
  if (p.y >= BUTTONS_Y) {
    return p.x < 67 ? UI_HIT_PREV : p.x < 134 ? UI_HIT_PLAY_PAUSE : UI_HIT_NEXT;
  }
  // The heart, with room around it for a fingertip.
  if (p.x < COVER_X + 6 && p.y >= 82 && p.y < 132) {
    return UI_HIT_HEART;
  }
  // The cover itself plays and pauses.
  if (p.y < TITLE_Y) {
    return UI_HIT_RECORD;
  }
  return UI_HIT_NONE;
}

#endif  // PBL_PLATFORM_EMERY
