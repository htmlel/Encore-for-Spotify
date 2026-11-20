// Now Playing for the Pebble Time 2.
//
// The album cover fills the whole screen, with the time and a heart (filled
// when the song is in Liked Songs) on top of it in the upper corners. The controls lie over the
// cover's lower half, which is darkened under them: title, artist, playing
// device, progress bar and touch buttons. While music plays and the watch is
// left alone they fade away, leaving the cover; a touch or a button brings
// them back.
//
// The cover comes in twice: a quick small version, drawn at double size, and
// then the full one.
//
// One of the two Now Playing looks on the Time 2 (see ui_time2.c); the other
// is ui_card.c.

#include <pebble.h>
#include "ui.h"
#include "comm.h"
#include "marquee.h"
#include "ui_looks.h"

#if defined(PBL_PLATFORM_EMERY)

#define SCREEN_W 200
#define SCREEN_H 228
#define COVER_SIZE 200      // the cover arrives as a square this size

#define FADE_TOP 96         // the cover starts to darken here...
#define FADE_BOTTOM 120     // ...and is fully darkened from here down
#define TITLE_Y 118
#define ARTIST_Y 145
#define DEVICE_Y 164
#define PROGRESS_Y 192
#define BUTTONS_Y 184       // touch buttons take the strip from here down
#define BUTTON_ICON_Y 214

#define HEART_CX 170   // in from the screen's rounded corner
#define HEART_CY 27

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

// The cover's standout color, used where the cover does not reach and while
// there is no cover.
static GColor background_for(GBitmap *bmp) {
  if (!bmp || gbitmap_get_format(bmp) != GBitmapFormat8Bit) {
    return GColorOxfordBlue;
  }
  // Count how much of the cover each of the screen's 64 colors takes up.
  static uint16_t counts[64];
  memset(counts, 0, sizeof(counts));
  const GRect b = gbitmap_get_bounds(bmp);
  const uint8_t *data = gbitmap_get_data(bmp);
  const int stride = gbitmap_get_bytes_per_row(bmp);
  for (int y = 0; y < b.size.h; y += 2) {
    for (int x = 0; x < b.size.w; x += 2) {
      counts[data[y * stride + x] & 0x3F]++;
    }
  }
  // The standout color is a common one with some color to it: grays, white
  // and black are passed over, and stronger colors count for more.
  int best = -1;
  uint32_t best_score = 0;
  for (int c = 0; c < 64; c++) {
    const int r = (c >> 4) & 3;
    const int g = (c >> 2) & 3;
    const int bl = c & 3;
    const int hi = r > g ? (r > bl ? r : bl) : (g > bl ? g : bl);
    const int lo = r < g ? (r < bl ? r : bl) : (g < bl ? g : bl);
    const int strength = hi - lo;  // 0 for a gray, 3 for a pure color
    if (strength == 0 || counts[c] == 0) {
      continue;
    }
    const uint32_t score = (uint32_t)counts[c] * (uint32_t)(strength * strength);
    if (score > best_score) {
      best_score = score;
      best = c;
    }
  }
  if (best < 0) {
    return GColorDarkGray;  // a cover with no color in it at all
  }
  int r = (best >> 4) & 3;
  int g = (best >> 2) & 3;
  int bl = best & 3;
  // Keep it deep enough for white text: bring a bright color down a step.
  if (r == 3 || g == 3 || bl == 3) {
    r = r > 0 ? r - 1 : 0;
    g = g > 0 ? g - 1 : 0;
    bl = bl > 0 ? bl - 1 : 0;
  }
  return (GColor){.argb = (uint8_t)(0xC0 | (r << 4) | (g << 2) | bl)};
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

// Draws the whole cover, as wide as the screen and centered top to bottom.
// The screen is a little taller than a square cover; the thin strips that
// leaves above and below are filled with a mirror image of the cover's own
// edge, so the picture runs to the edge of the screen with nothing cropped,
// stretched or barred. Works for the quick preview and the full cover alike.
static void draw_cover_filling(GContext *ctx, GBitmap *bmp) {
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
  const int top = (SCREEN_H - SCREEN_W) / 2;  // the strip above the cover
  for (int y = 0; y < SCREEN_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    int cy = y - top;
    // Above and below the cover, its own edge is continued as a mirror image.
    if (cy < 0) {
      cy = -cy - 1;
    } else if (cy >= SCREEN_W) {
      cy = 2 * SCREEN_W - 1 - cy;
    }
    const uint8_t *line = src + (cy * b.size.h / SCREEN_W) * stride;
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      row.data[x] = line[x * b.size.w / SCREEN_W];
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static void draw_cover(GContext *ctx) {
  if (s_art) {
    draw_cover_filling(ctx, s_art);
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

// Text with a black edge, so it reads on any cover.
static void draw_edged_text(GContext *ctx, const char *text, GFont font, GRect r,
                            GTextAlignment align) {
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx || dy) {
        graphics_draw_text(ctx, text, font, GRect(r.origin.x + dx, r.origin.y + dy, r.size.w, r.size.h),
                           GTextOverflowModeFill, align, NULL);
      }
    }
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, r, GTextOverflowModeFill, align, NULL);
}

// A heart from two circles and a triangle, centered on (cx, cy).
static void draw_heart_shape(GContext *ctx, int cx, int cy, int grow) {
  const int r = 3 + grow;
  graphics_fill_circle(ctx, GPoint(cx - 4, cy - 2), r);
  graphics_fill_circle(ctx, GPoint(cx + 4, cy - 2), r);
  // Close the gap the two circles leave between them.
  graphics_fill_rect(ctx, GRect(cx - 2, cy - 3, 5, 4), 0, GCornerNone);
  GPathInfo info = {
      .num_points = 3,
      .points = (GPoint[]){{-7 - grow, 0}, {7 + grow, 0}, {0, 8 + grow}},
  };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_move_to(path, GPoint(cx, cy));
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

// The time and heart are drawn over the top strip of the cover, and stay at
// full strength whether or not the controls are showing. A copy of the strip
// from before they were drawn lets the middle of an unliked heart be put
// back afterwards, so the cover shows through it.
#define TOP_STRIP_H 44
#define SEE_THROUGH GColorMagentaARGB8   // stands in for "cover shows here"
static uint8_t s_top_strip[TOP_STRIP_H * SCREEN_W];

static void save_top_strip(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = 0; y < TOP_STRIP_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = 0; x < SCREEN_W; x++) {
      s_top_strip[y * SCREEN_W + x] = (x >= row.min_x && x <= row.max_x) ? row.data[x] : 0xC0;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static void finish_top_strip(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = 0; y < TOP_STRIP_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      const uint8_t before = s_top_strip[y * SCREEN_W + x];
      const bool in_heart = x > HEART_CX - 18 && x < HEART_CX + 18;
      if (in_heart && row.data[x] == SEE_THROUGH) {
        row.data[x] = before;
      }
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static void draw_heart(GContext *ctx) {
  const bool liked = comm_get_liked() == 1;
  // Straight on the cover, so it has to read on white and on black alike: a
  // white band with a black line on both sides of it. The middle is red when
  // the song is liked; otherwise the cover shows through.
  graphics_context_set_fill_color(ctx, GColorBlack);
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 5);
  graphics_context_set_fill_color(ctx, GColorWhite);
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 4);
  graphics_context_set_fill_color(ctx, GColorBlack);
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 2);
  // Hard edges here, so every pixel of the middle is exactly the stand-in
  // color that finish_top_strip() looks for.
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_fill_color(ctx, liked ? GColorRed : (GColor){.argb = SEE_THROUGH});
  draw_heart_shape(ctx, HEART_CX, HEART_CY, 1);
  graphics_context_set_antialiased(ctx, true);
}

// The time, and shuffle and repeat when they are on, over the top of the cover.
static void draw_bar(GContext *ctx) {
  draw_edged_text(ctx, s_clock, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                  GRect(12, 16, 60, 22), GTextAlignmentLeft);  // level with the heart
  char modes[20] = "";
  if (s_shuffle) {
    strcat(modes, "SHUF");
  }
  if (s_repeat) {
    strcat(modes, modes[0] ? "  " : "");
    strcat(modes, s_repeat == 2 ? "RPT 1" : "RPT");
  }
  if (modes[0]) {
    draw_edged_text(ctx, modes, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                    GRect(60, 19, 90, 18), GTextAlignmentCenter);
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
  save_top_strip(ctx);
  draw_bar(ctx);
  draw_heart(ctx);
  finish_top_strip(ctx);
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
void cover_wake(void) {
  fade_to(true);
  if (s_hide_timer) {
    app_timer_reschedule(s_hide_timer, CONTROLS_STAY_MS);
  } else if (s_canvas) {
    s_hide_timer = app_timer_register(CONTROLS_STAY_MS, hide_timeout, NULL);
  }
}

bool cover_controls_shown(void) {
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

void cover_init(Window *window) {
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
  cover_wake();
}

void cover_deinit(void) {
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

void cover_start_loading(void) {
  // Whatever is known shows at once; the cover arrives with the first report.
}

void cover_set_album_art(GBitmap *bitmap) {
  s_art = bitmap;
  s_bg = background_for(bitmap);
  // The previous cover is no longer drawn, so comm.c may free it.
  comm_release_old_art();
  redraw();
}

void cover_clear_album_art_instant(void) {
  s_art = NULL;
  s_bg = GColorOxfordBlue;
  redraw();
}

static void volume_hide(void *data) {
  s_volume_timer = NULL;
  s_volume_text[0] = '\0';
  redraw();
}

void cover_set_status(const char *text) {
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
  cover_wake();
  redraw();
}

void cover_set_track_info(const char *title, const char *artist) {
  if (strcmp(s_title, title) != 0) {
    strncpy(s_title, title, sizeof(s_title) - 1);
    s_title[sizeof(s_title) - 1] = '\0';
    if (s_title_layer) {
      marquee_layer_set_text(s_title_layer, s_title);
    }
    cover_wake();  // a new song: show what it is
  }
  strncpy(s_artist, artist, sizeof(s_artist) - 1);
  s_artist[sizeof(s_artist) - 1] = '\0';
  redraw();
}

void cover_set_progress(int elapsed_sec, int total_sec) {
  s_elapsed = elapsed_sec;
  s_total = total_sec;
  // Nothing to repaint each second while only the cover shows.
  if (s_controls > 0) {
    redraw();
  }
}

void cover_set_shuffle(bool on) {
  s_shuffle = on;
  redraw();
}

void cover_set_repeat(int state) {
  s_repeat = state;
  redraw();
}

void cover_hint_animation_dir(bool reverse) {
  (void)reverse;  // this screen has no slide-in animation
}

void cover_set_playing(bool playing) {
  if (playing != s_playing) {
    s_playing = playing;
    cover_wake();  // paused: they stay; playing again: they fade after a while
  }
  redraw();
}

void cover_refresh(void) {
  redraw();
}

UiHit cover_hit(GPoint p) {
  if (p.y >= BUTTONS_Y) {
    return p.x < 67 ? UI_HIT_PREV : p.x < 134 ? UI_HIT_PLAY_PAUSE : UI_HIT_NEXT;
  }
  // The heart, with room around it for a fingertip.
  if (p.x >= SCREEN_W - 70 && p.y < 62) {
    return UI_HIT_HEART;
  }
  // The clear part of the cover plays and pauses.
  if (p.y < TITLE_Y) {
    return UI_HIT_RECORD;
  }
  return UI_HIT_NONE;
}

#endif  // PBL_PLATFORM_EMERY
