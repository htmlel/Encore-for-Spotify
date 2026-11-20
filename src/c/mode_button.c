#include "mode_button.h"

#define FLIP_STEPS 8     // even: the face changes at the halfway step
#define FLIP_MS 30

static bool s_is_repeat;           // which face is up
static int s_flip_step = -1;       // -1 when not flipping
static AppTimer *s_flip_timer;
static void (*s_redraw)(void);

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

// Both signs are drawn around a center, with every sideways distance scaled
// by `scale` sixteenths, which is what makes the flip.
static void draw_shuffle_sign(GContext *ctx, int cx, int cy, int scale, GColor color) {
  #define SX(dx) (cx + (dx) * scale / 16)
  const int top = cy - 5;
  const int bottom = cy + 5;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  // Two arrows that run level, swap heights in the middle, and run level
  // again: one from the top left to the bottom right, one the other way.
  graphics_draw_line(ctx, GPoint(SX(-12), top), GPoint(SX(-7), top));
  graphics_draw_line(ctx, GPoint(SX(-7), top), GPoint(SX(0), bottom));
  graphics_draw_line(ctx, GPoint(SX(0), bottom), GPoint(SX(5), bottom));
  graphics_draw_line(ctx, GPoint(SX(-12), bottom), GPoint(SX(-7), bottom));
  graphics_draw_line(ctx, GPoint(SX(-7), bottom), GPoint(SX(0), top));
  graphics_draw_line(ctx, GPoint(SX(0), top), GPoint(SX(5), top));
  graphics_context_set_fill_color(ctx, color);
  const int head = 7 * scale / 16;
  if (head > 0) {
    draw_triangle(ctx, SX(5), top - 5, head, 10, true);
    draw_triangle(ctx, SX(5), bottom - 5, head, 10, true);
  }
  #undef SX
}

static void draw_repeat_sign(GContext *ctx, int cx, int cy, int scale, GColor color, bool one) {
  #define SX(dx) (cx + (dx) * scale / 16)
  const int top = cy - 5;
  const int bottom = cy + 5;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_fill_color(ctx, color);
  const int head = 7 * scale / 16;
  if (one) {
    // Repeating one song, drawn the way Spotify does: a closed loop with a
    // gap in the top where a 1 sits, and one arrowhead on the bottom run.
    graphics_draw_line(ctx, GPoint(SX(-5), top), GPoint(SX(-9), top));
    graphics_draw_line(ctx, GPoint(SX(-9), top), GPoint(SX(-11), top + 2));
    graphics_draw_line(ctx, GPoint(SX(-11), top + 2), GPoint(SX(-11), bottom - 2));
    graphics_draw_line(ctx, GPoint(SX(-11), bottom - 2), GPoint(SX(-9), bottom));
    graphics_draw_line(ctx, GPoint(SX(5), top), GPoint(SX(9), top));
    graphics_draw_line(ctx, GPoint(SX(9), top), GPoint(SX(11), top + 2));
    graphics_draw_line(ctx, GPoint(SX(11), top + 2), GPoint(SX(11), bottom - 2));
    graphics_draw_line(ctx, GPoint(SX(11), bottom - 2), GPoint(SX(9), bottom));
    graphics_draw_line(ctx, GPoint(SX(9), bottom), GPoint(SX(4), bottom));
    graphics_draw_line(ctx, GPoint(SX(-9), bottom), GPoint(SX(-3), bottom));
    if (head > 0) {
      draw_triangle(ctx, SX(3) - head, bottom - 5, head, 10, false);
    }
    if (scale >= 12) {
      graphics_draw_line(ctx, GPoint(cx, top - 4), GPoint(cx, top + 4));
      graphics_draw_line(ctx, GPoint(cx - 2, top - 2), GPoint(cx, top - 4));
    }
  } else {
    // Two arrows chasing each other round a loop, with a gap between the
    // tail of one and the head of the other: up the left side and along the
    // top to a head pointing right, down the right side and back along the
    // bottom to a head pointing left.
    graphics_draw_line(ctx, GPoint(SX(-11), cy - 1), GPoint(SX(-11), top + 2));
    graphics_draw_line(ctx, GPoint(SX(-11), top + 2), GPoint(SX(-9), top));
    graphics_draw_line(ctx, GPoint(SX(-9), top), GPoint(SX(5), top));
    graphics_draw_line(ctx, GPoint(SX(11), cy + 1), GPoint(SX(11), bottom - 2));
    graphics_draw_line(ctx, GPoint(SX(11), bottom - 2), GPoint(SX(9), bottom));
    graphics_draw_line(ctx, GPoint(SX(9), bottom), GPoint(SX(-5), bottom));
    if (head > 0) {
      draw_triangle(ctx, SX(5), top - 5, head, 10, true);
      draw_triangle(ctx, SX(-5) - head, bottom - 5, head, 10, false);
    }
  }
  #undef SX
}

void mode_button_draw(GContext *ctx, GPoint center, bool shuffle, int repeat, GColor on,
                      GPoint dot) {
  // During a flip: full width at the ends, nothing in the middle, and the
  // other face from the halfway step on.
  int scale = 16;
  bool repeat_face = s_is_repeat;
  if (s_flip_step >= 0) {
    const int half = FLIP_STEPS / 2;
    const int from_middle = s_flip_step < half ? half - s_flip_step : s_flip_step - half;
    scale = 16 * from_middle / half;
    // s_is_repeat already names the new face; the old one shows first.
    repeat_face = s_flip_step < half ? !s_is_repeat : s_is_repeat;
  }
  if (scale <= 0) {
    return;
  }
  if (repeat_face) {
    draw_repeat_sign(ctx, center.x, center.y, scale, repeat ? on : GColorWhite, repeat == 2);
  } else {
    draw_shuffle_sign(ctx, center.x, center.y, scale, shuffle ? on : GColorWhite);
  }
  if (repeat_face ? shuffle : repeat != 0) {
    const GPoint at = GPoint(center.x + dot.x, center.y + dot.y);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, at, 5);
    graphics_context_set_fill_color(ctx, on);
    graphics_fill_circle(ctx, at, 3);
  }
}

bool mode_button_is_repeat(void) {
  return s_is_repeat;
}

static void flip_step(void *data) {
  s_flip_timer = NULL;
  s_flip_step++;
  if (s_flip_step > FLIP_STEPS) {
    s_flip_step = -1;
  } else {
    s_flip_timer = app_timer_register(FLIP_MS, flip_step, NULL);
  }
  if (s_redraw) {
    s_redraw();
  }
}

void mode_button_flip(void (*redraw)(void)) {
  if (s_flip_timer) {
    return;  // already turning over
  }
  s_redraw = redraw;
  s_is_repeat = !s_is_repeat;
  s_flip_step = 0;
  s_flip_timer = app_timer_register(FLIP_MS, flip_step, NULL);
  if (s_redraw) {
    s_redraw();
  }
}

void mode_button_stop(void) {
  if (s_flip_timer) {
    app_timer_cancel(s_flip_timer);
    s_flip_timer = NULL;
  }
  s_flip_step = -1;
  s_redraw = NULL;
}
