// Now Playing for the Pebble Time 2.
//
// The album cover fills the screen under a slim top bar with the time and a
// heart (filled when the song is in Liked Songs). The controls lie over the
// cover's lower half, which is darkened under them: title, artist, playing
// device, progress bar and touch buttons. While music plays and the watch is
// left alone they fade away, leaving the cover; a touch or a button brings
// them back.
//
// The cover comes in twice: a quick small version, drawn at double size, and
// then the full one.
//
// This file replaces ui.c on the Time 2 (ui.c is the original screen, still
// used for the other watches if the app is ever built for them).

#include <pebble.h>
#include "ui.h"
#include "comm.h"
#include "marquee.h"

#if defined(PBL_PLATFORM_EMERY)

#define SCREEN_W 200
#define SCREEN_H 228
#define COVER_SIZE 200      // the cover arrives as a square this size
#define BAR_H 28            // the top bar; the cover takes the rest

#define FADE_TOP 96         // the cover starts to darken here...
#define FADE_BOTTOM 120     // ...and is fully darkened from here down
#define TITLE_Y 118
#define ARTIST_Y 145
#define DEVICE_Y 164
#define PROGRESS_Y 192
#define BUTTONS_Y 184       // touch buttons take the strip from here down
#define BUTTON_ICON_Y 214

#define HEART_CX 180
#define HEART_CY 12

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

// The controls over the cover: 16 when fully shown, 0 when faded out.
#define CONTROLS_STAY_MS 6000
#define CONTROLS_FADE_STEP 4
#define CONTROLS_FADE_MS 40
static int s_controls = 16;
static bool s_controls_wanted = true;
static AppTimer *s_hide_timer;
static AppTimer *s_fade_timer;

// The volume just set, shown for a moment where the device name is. A notice
// would get in the way of pressing the button again.
#define VOLUME_SHOW_MS 2500
static char s_volume_text[20];
static AppTimer *s_volume_timer;

// --- Colors -----------------------------------------------------------------

// A dark tint of the cover, used where the cover does not reach and while
// there is no cover.
static GColor background_for(GBitmap *bmp) {
  if (!bmp || gbitmap_get_format(bmp) != GBitmapFormat8Bit) {
    return GColorOxfordBlue;
  }
  const GRect b = gbitmap_get_bounds(bmp);
  const uint8_t *data = gbitmap_get_data(bmp);
  const int stride = gbitmap_get_bytes_per_row(bmp);
  int r = 0, g = 0, bl = 0, n = 0;
  for (int y = 0; y < b.size.h; y += 4) {
    for (int x = 0; x < b.size.w; x += 4) {
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
    return GColorDarkGray;   // a black cover still gets a bar that shows
  }
  return (GColor){.argb = (uint8_t)(0xC0 | (cr << 4) | (cg << 2) | cb)};
}

// --- Drawing ----------------------------------------------------------------

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Halves the brightness of what is on screen from FADE_BOTTOM down, with a
// dithered run-in from FADE_TOP, so text on top of the cover stays readable.
static void darken_lower_half(GContext *ctx) {
  if (s_controls <= 0) {
    return;
  }
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = FADE_TOP; y < SCREEN_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    // How many of every 16 pixels to darken on this row; fewer while the
    // controls are fading in or out.
    const int full = y >= FADE_BOTTOM ? 16 : (y - FADE_TOP) * 16 / (FADE_BOTTOM - FADE_TOP);
    const int level = full * s_controls / 16;
    for (int x = row.min_x; x <= row.max_x; x++) {
      if (BAYER4[y & 3][x & 3] < level) {
        const uint8_t px = row.data[x];
        row.data[x] = 0xC0 | (((px >> 4) & 3) >> 1) << 4 | (((px >> 2) & 3) >> 1) << 2 |
                      ((px & 3) >> 1);
      }
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// Draws a cover that is smaller than the screen stretched to fill it: the
// quick preview that arrives before the full cover.
static void draw_cover_stretched(GContext *ctx, GBitmap *bmp) {
  const GRect b = gbitmap_get_bounds(bmp);
  if (gbitmap_get_format(bmp) != GBitmapFormat8Bit || b.size.w <= 0 || b.size.h <= 0) {
    return;
  }
  const uint8_t *src = gbitmap_get_data(bmp);
  const int stride = gbitmap_get_bytes_per_row(bmp);
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = BAR_H; y < SCREEN_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    const uint8_t *line = src + ((y - BAR_H) * b.size.h / COVER_SIZE) * stride;
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      row.data[x] = line[x * b.size.w / COVER_SIZE];
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static void draw_cover(GContext *ctx) {
  if (s_art) {
    const GRect b = gbitmap_get_bounds(s_art);
    if (b.size.w < COVER_SIZE) {
      draw_cover_stretched(ctx, s_art);
    } else {
      graphics_context_set_compositing_mode(ctx, GCompOpAssign);
      graphics_draw_bitmap_in_rect(ctx, s_art, GRect(0, BAR_H, b.size.w, b.size.h));
    }
    darken_lower_half(ctx);
    return;
  }
  // No cover yet: a note where it will be.
  const int cx = SCREEN_W / 2;
  const int cy = 72;
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_circle(ctx, GPoint(cx - 8, cy + 20), 12);
  graphics_fill_rect(ctx, GRect(cx - 1, cy - 30, 6, 52), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx - 1, cy - 30, 26, 10), 0, GCornerNone);
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
  const bool liked = comm_get_liked() == 1;
  // An outline: the shape a size up in white, then the middle filled in red
  // when liked or with the bar's color when not.
  graphics_context_set_fill_color(ctx, GColorWhite);
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 2);
  graphics_context_set_fill_color(ctx, liked ? GColorRed : s_bg);
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 0);
}

static void draw_bar(GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, BAR_H), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_clock, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(6, 0, 60, 22), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  // Shuffle and repeat, in the middle of the bar, only when they are on.
  char modes[20] = "";
  if (s_shuffle) {
    strcat(modes, "SHUF");
  }
  if (s_repeat) {
    strcat(modes, modes[0] ? "  " : "");
    strcat(modes, s_repeat == 2 ? "RPT 1" : "RPT");
  }
  if (modes[0]) {
    graphics_draw_text(ctx, modes, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(60, 4, 96, 18), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
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
    graphics_context_set_fill_color(ctx, GColorDarkGray);
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
  draw_bar(ctx);
  draw_heart(ctx);
  // The text and buttons come and go halfway through the fade.
  if (s_controls >= 8) {
    draw_text_and_progress(ctx);
    draw_buttons(ctx);
  }
}

static void redraw(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

// --- Showing and hiding the controls ----------------------------------------

static void apply_controls(void) {
  if (s_title_layer) {
    layer_set_hidden(marquee_layer_get_layer(s_title_layer), s_controls < 8);
  }
  redraw();
}

static void fade_step(void *data) {
  s_fade_timer = NULL;
  const int target = s_controls_wanted ? 16 : 0;
  if (s_controls < target) {
    s_controls += CONTROLS_FADE_STEP;
  } else if (s_controls > target) {
    s_controls -= CONTROLS_FADE_STEP;
  }
  apply_controls();
  if (s_controls != target && s_canvas) {
    s_fade_timer = app_timer_register(CONTROLS_FADE_MS, fade_step, NULL);
  }
}

static void fade_to(bool shown) {
  s_controls_wanted = shown;
  if (!s_canvas) {
    s_controls = shown ? 16 : 0;
    return;
  }
  if (!s_fade_timer && s_controls != (shown ? 16 : 0)) {
    s_fade_timer = app_timer_register(CONTROLS_FADE_MS, fade_step, NULL);
  }
}

static void hide_timeout(void *data) {
  s_hide_timer = NULL;
  // Only with a cover to look at and music playing; otherwise they stay.
  if (s_art && s_playing) {
    fade_to(false);
  }
}

// Brings the controls back and restarts the wait before they fade again.
void ui_wake(void) {
  fade_to(true);
  if (s_hide_timer) {
    app_timer_reschedule(s_hide_timer, CONTROLS_STAY_MS);
  } else if (s_canvas) {
    s_hide_timer = app_timer_register(CONTROLS_STAY_MS, hide_timeout, NULL);
  }
}

bool ui_controls_shown(void) {
  return s_controls_wanted;
}

// --- Clock ------------------------------------------------------------------

static void set_clock(struct tm *t) {
  strftime(s_clock, sizeof(s_clock), clock_is_24h_style() ? "%H:%M" : "%l:%M", t);
  // "%l" pads single-digit hours with a space; drop it.
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
  s_controls = 16;
  ui_wake();
}

void ui_deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_volume_timer) {
    app_timer_cancel(s_volume_timer);
    s_volume_timer = NULL;
  }
  s_volume_text[0] = '\0';
  if (s_hide_timer) {
    app_timer_cancel(s_hide_timer);
    s_hide_timer = NULL;
  }
  if (s_fade_timer) {
    app_timer_cancel(s_fade_timer);
    s_fade_timer = NULL;
  }
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
  ui_wake();
  redraw();
}

void ui_set_track_info(const char *title, const char *artist) {
  if (strcmp(s_title, title) != 0) {
    strncpy(s_title, title, sizeof(s_title) - 1);
    s_title[sizeof(s_title) - 1] = '\0';
    if (s_title_layer) {
      marquee_layer_set_text(s_title_layer, s_title);
    }
    ui_wake();  // a new song: show what it is
  }
  strncpy(s_artist, artist, sizeof(s_artist) - 1);
  s_artist[sizeof(s_artist) - 1] = '\0';
  redraw();
}

void ui_set_progress(int elapsed_sec, int total_sec) {
  s_elapsed = elapsed_sec;
  s_total = total_sec;
  // Nothing to repaint each second while only the cover shows.
  if (s_controls > 0) {
    redraw();
  }
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
  if (playing != s_playing) {
    s_playing = playing;
    ui_wake();  // paused: they stay; playing again: they fade after a while
  }
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
  if (p.x >= SCREEN_W - 64 && p.y < 52) {
    return UI_HIT_HEART;
  }
  // The clear part of the cover plays and pauses.
  if (p.y < TITLE_Y) {
    return UI_HIT_RECORD;
  }
  return UI_HIT_NONE;
}

#endif  // PBL_PLATFORM_EMERY
