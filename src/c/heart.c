#include "heart.h"

// The heart is two round lobes over a point with straight sides. Each pixel
// of a small grid is marked as edge, line or middle: the line is every pixel
// of the heart within `thickness` of its outside, which is what keeps the
// line the same thickness everywhere, lobes, notch and point alike.
#define GRID 44
#define HALF (GRID / 2)

enum { OUTSIDE = 0, EDGE, LINE, MIDDLE };

static uint8_t s_grid[GRID * GRID];
static int s_grid_scale = -1;
static int s_grid_thickness = -1;

static bool in_heart(int gx, int gy, int scale) {
  // Worked in half pixels, in which `scale` is the radius of one lobe.
  const int32_t x = 2 * (gx - HALF);
  const int32_t ax = x < 0 ? -x : x;
  // Raised so that the whole heart, lobes to point, sits centered.
  const int32_t y = 2 * (gy - HALF) + scale * 7 / 10;
  // The two round lobes, side by side and touching in the middle.
  if ((ax - scale) * (ax - scale) + y * y <= scale * scale) {
    return true;
  }
  // The lower part: straight sides running at 45 degrees from where they
  // touch the lobes down to the point.
  return y >= 0 && y >= ax - scale && (ax + y) * 1000 <= scale * 2414;
}

static void build(int scale, int thickness) {
  static uint8_t inside[GRID * GRID];
  for (int gy = 0; gy < GRID; gy++) {
    for (int gx = 0; gx < GRID; gx++) {
      inside[gy * GRID + gx] = in_heart(gx, gy, scale);
    }
  }
  // The curve ends in a hair-thin point; trim the single-pixel tail so the
  // heart ends on a blunt tip instead of a spike.
  for (int gy = 0; gy < GRID; gy++) {
    for (int gx = 1; gx < GRID - 1; gx++) {
      if (inside[gy * GRID + gx] && !inside[gy * GRID + gx - 1] && !inside[gy * GRID + gx + 1]) {
        inside[gy * GRID + gx] = 0;
      }
    }
  }
  for (int gy = 0; gy < GRID; gy++) {
    for (int gx = 0; gx < GRID; gx++) {
      const bool in = inside[gy * GRID + gx];
      // How far to look: the line's thickness from inside, one pixel from
      // outside (for the edge).
      const int reach = in ? thickness : 1;
      bool other_near = false;
      for (int dy = -reach; dy <= reach && !other_near; dy++) {
        for (int dx = -reach; dx <= reach; dx++) {
          if (dx * dx + dy * dy > reach * reach + reach / 2) {
            continue;
          }
          const int nx = gx + dx;
          const int ny = gy + dy;
          const bool n_in = nx >= 0 && nx < GRID && ny >= 0 && ny < GRID && inside[ny * GRID + nx];
          if (n_in != in) {
            other_near = true;
            break;
          }
        }
      }
      s_grid[gy * GRID + gx] = in ? (other_near ? LINE : MIDDLE) : (other_near ? EDGE : OUTSIDE);
    }
  }
  s_grid_scale = scale;
  s_grid_thickness = thickness;
}

void heart_draw(GContext *ctx, GPoint center, int scale, int thickness, GColor line, bool edged,
                bool filled, GColor fill) {
  if (scale != s_grid_scale || thickness != s_grid_thickness) {
    build(scale, thickness);
  }
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  const GRect bounds = gbitmap_get_bounds(fb);
  for (int gy = 0; gy < GRID; gy++) {
    const int y = center.y - HALF + gy;
    if (y < 0 || y >= bounds.size.h) {
      continue;
    }
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int gx = 0; gx < GRID; gx++) {
      const int x = center.x - HALF + gx;
      if (x < row.min_x || x > row.max_x) {
        continue;
      }
      switch (s_grid[gy * GRID + gx]) {
        case LINE:
          row.data[x] = line.argb;
          break;
        case MIDDLE:
          if (filled) {
            row.data[x] = fill.argb;
          }
          break;
        case EDGE:
          if (edged) {
            row.data[x] = GColorBlackARGB8;
          }
          break;
        default:
          break;
      }
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}
