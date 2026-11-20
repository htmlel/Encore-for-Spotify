#pragma once
#include <pebble.h>

// Draws a heart outline whose line is the same thickness all the way round,
// straight into the screen's pixels, centered on `center`.
//   scale:     size; the heart is about 1.8 * scale pixels wide
//   thickness: of the line, in pixels
//   line:      the line's color
//   edged:     a one-pixel black edge around the outside, for busy pictures
//   filled:    the middle takes `fill`; otherwise what is behind shows through
void heart_draw(GContext *ctx, GPoint center, int scale, int thickness, GColor line, bool edged,
                bool filled, GColor fill);
