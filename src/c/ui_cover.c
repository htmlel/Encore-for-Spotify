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
#include "heart.h"

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

// The heart sits to the right of the song name and artist, and comes and
// goes with them.
#define HEART_CX 181
#define HEART_CY 161
#define TEXT_SIDE 32        // kept clear on both sides of the name, so it stays centered

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

// The screen is 28 pixels taller than a square cover. The whole cover is
// drawn in the middle, nothing cropped, and the 14-pixel strips above and
// below it are a dithered stretch of the cover's own edges: each edge pixel
// is pulled straight out to the edge of the screen, and more and more of
// the stretched pixels are dropped to black the further out they are, in a
// regular dot pattern, so the picture fades off into the watch's bezel.
// Works for the quick preview and the full cover alike.
#define COVER_TOP ((SCREEN_H - SCREEN_W) / 2)

// Fills screen rows first_y..last_y by stretching cover row edge_y.
// `near_y` is the screen row touching the cover.
static void stretch_edge(GBitmap *fb, GBitmap *bmp, int edge_y, int first_y, int last_y,
                         int near_y) {
  const GRect b = gbitmap_get_bounds(bmp);
  const uint8_t *line =
      gbitmap_get_data(bmp) + (edge_y * b.size.h / SCREEN_W) * gbitmap_get_bytes_per_row(bmp);
  // A 4x4 pattern that decides which pixels go black first.
  static const uint8_t pattern[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int y = first_y; y <= last_y; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    // 1 next to the cover, COVER_TOP at the edge of the screen; that many
    // sixteenths of the pixels are black.
    const int away = 1 + (y > near_y ? y - near_y : near_y - y);
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      row.data[x] = pattern[y & 3][x & 3] < away ? GColorBlackARGB8 : line[x * b.size.w / SCREEN_W];
    }
  }
}

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
  for (int y = COVER_TOP; y < COVER_TOP + SCREEN_W; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    const uint8_t *line = src + ((y - COVER_TOP) * b.size.h / SCREEN_W) * stride;
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      row.data[x] = line[x * b.size.w / SCREEN_W];
    }
  }
  stretch_edge(fb, bmp, 0, 0, COVER_TOP - 1, COVER_TOP - 1);
  stretch_edge(fb, bmp, SCREEN_W - 1, COVER_TOP + SCREEN_W, SCREEN_H - 1, COVER_TOP + SCREEN_W);
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

// The time is always white, in the strip along the top, with a
// thin dark line around them so they read on a white cover too.
//
// They are first drawn in stand-in colors no cover pixel has (any that did
// are nudged to a neighboring color first), so that afterwards every pixel
// of them can be found, the dark line laid around them, and the stand-ins
// turned into the real white and red.
#define TOP_STRIP_H 24
#define INK_WHITE GColorMagentaARGB8
#define INK_RED GColorShockingPinkARGB8
#define INK_HOLE GColorRichBrilliantLavenderARGB8   // the middle of an unliked heart
static uint8_t s_ink_distance[TOP_STRIP_H * SCREEN_W];
static uint8_t s_top_strip[TOP_STRIP_H * SCREEN_W];  // the cover before anything is drawn on it

static void prepare_top_strip(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  for (int y = 0; y < TOP_STRIP_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      if (row.data[x] == INK_WHITE || row.data[x] == INK_RED || row.data[x] == INK_HOLE) {
        row.data[x] = GColorPurpleARGB8;
      }
      s_top_strip[y * SCREEN_W + x] = row.data[x];
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// Much darker: each color channel has four levels, and only the top one
// keeps anything.
static uint8_t darkest(uint8_t c) {
  const uint8_t r = (c >> 4) & 3, g = (c >> 2) & 3, b = c & 3;
  return (uint8_t)(0xC0 | ((r / 3) << 4) | ((g / 3) << 2) | (b / 3));
}

static void finish_top_strip(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  // How far each pixel is from the nearest pixel of the time or heart, in
  // half pixels (2 for a step sideways, 3 for a diagonal one): one sweep
  // down the strip and one back up.
  for (int y = 0; y < TOP_STRIP_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = 0; x < SCREEN_W; x++) {
      const bool ink = x >= row.min_x && x <= row.max_x &&
                       (row.data[x] == INK_WHITE || row.data[x] == INK_RED);
      s_ink_distance[y * SCREEN_W + x] = ink ? 0 : 100;
    }
  }
  #define D(xx, yy) s_ink_distance[(yy) * SCREEN_W + (xx)]
  #define LOWER(xx, yy, add) \
    do { if (D(xx, yy) + (add) < d) d = D(xx, yy) + (add); } while (0)
  for (int y = 0; y < TOP_STRIP_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      int d = D(x, y);
      if (x > 0) LOWER(x - 1, y, 2);
      if (y > 0) LOWER(x, y - 1, 2);
      if (x > 0 && y > 0) LOWER(x - 1, y - 1, 3);
      if (x < SCREEN_W - 1 && y > 0) LOWER(x + 1, y - 1, 3);
      D(x, y) = (uint8_t)d;
    }
  }
  for (int y = TOP_STRIP_H - 1; y >= 0; y--) {
    for (int x = SCREEN_W - 1; x >= 0; x--) {
      int d = D(x, y);
      if (x < SCREEN_W - 1) LOWER(x + 1, y, 2);
      if (y < TOP_STRIP_H - 1) LOWER(x, y + 1, 2);
      if (x < SCREEN_W - 1 && y < TOP_STRIP_H - 1) LOWER(x + 1, y + 1, 3);
      if (x > 0 && y < TOP_STRIP_H - 1) LOWER(x - 1, y + 1, 3);
      D(x, y) = (uint8_t)d;
    }
  }
  // A thin dark line right against the letters and the heart.
  for (int y = 0; y < TOP_STRIP_H; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = row.min_x; x <= row.max_x && x < SCREEN_W; x++) {
      const int d = D(x, y);
      if (d == 0) {
        row.data[x] = row.data[x] == INK_RED ? GColorRedARGB8 : GColorWhiteARGB8;
      } else if (row.data[x] == INK_HOLE) {
        // Inside the heart the cover shows, with only the thin dark line
        // against the band.
        const uint8_t cover = s_top_strip[y * SCREEN_W + x];
        row.data[x] = d <= 2 ? darkest(cover) : cover;
      } else if (d <= 3) {
        row.data[x] = darkest(row.data[x]);
      }
    }
  }
  #undef LOWER
  #undef D
  graphics_release_frame_buffer(ctx, fb);
}

static void draw_heart(GContext *ctx) {
  const bool liked = comm_get_liked() == 1;
  // A white band on the darkened part of the cover; the middle is red when
  // the song is liked, and otherwise the cover shows through.
  heart_draw(ctx, GPoint(HEART_CX, HEART_CY), 10, 2, GColorWhite, true, liked, GColorRed);
}

// Along the top: the time in the middle, with shuffle to its left and repeat
// to its right when they are on.
static void draw_bar(GContext *ctx) {
  graphics_context_set_text_color(ctx, (GColor){.argb = INK_WHITE});
  graphics_draw_text(ctx, s_clock, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(60, -5, 80, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  if (s_shuffle) {
    graphics_draw_text(ctx, "SHUF", small, GRect(12, -3, 50, 18), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
  if (s_repeat) {
    graphics_draw_text(ctx, s_repeat == 2 ? "RPT 1" : "RPT", small, GRect(138, -3, 50, 18),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
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
                     GRect(TEXT_SIDE, ARTIST_Y, SCREEN_W - 2 * TEXT_SIDE, 22), GTextOverflowModeTrailingEllipsis,
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
  prepare_top_strip(ctx);
  draw_bar(ctx);
  finish_top_strip(ctx);
  // The text, heart and buttons come and go halfway through the fade.
  if (s_controls >= 8) {
    draw_text_and_progress(ctx);
    draw_heart(ctx);
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

  s_title_layer = marquee_layer_create(GRect(TEXT_SIDE, TITLE_Y, bounds.size.w - 2 * TEXT_SIDE, 30));
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
  if (p.x >= SCREEN_W - 2 * TEXT_SIDE && p.y >= TITLE_Y - 4 && p.y < DEVICE_Y + 6) {
    return UI_HIT_HEART;
  }
  // The clear part of the cover plays and pauses.
  if (p.y < TITLE_Y) {
    return UI_HIT_RECORD;
  }
  return UI_HIT_NONE;
}

#endif  // PBL_PLATFORM_EMERY
