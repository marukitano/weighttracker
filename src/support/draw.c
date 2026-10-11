#include <pebble.h>
#include <math.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/draw.h"
#include "../support/data.h"
#include "../support/parameters.h"

// Time 2 chart: the retro Bayer dither is adapted from OpenLibreLinkUp.
// The fill grows denser from left (older) to right (newer), with no min/max labels.
#define CHART_LEFT 9
#define CHART_TOP 7
#define CHART_RIGHT 7
#define CHART_BOTTOM 25
#define TICK_SHORT 2
#define TICK_LONG 4

// Temporary preview: draw sample measurements without saving them.
#define FORCE_DEMO_CHART 1
static double s_multiplier = 1.0;
static uint8_t s_pulse_step = 0;

void set_chart_pulse_step(uint8_t step) {
  s_pulse_step = step % 40;
}

// Preview only: used when no actual measurements exist. Never persisted.
static const double DEMO_WEIGHTS[15] = {
  101.2, 103.8, 106.5, 104.1, 107.6,
  109.3, 105.7, 102.4, 108.1, 110.0,
  106.9, 103.2, 107.4, 104.8, 109.1
};

static const uint8_t DITHER_8X8[8][8] = {
  { 0, 32, 8, 40, 2, 34, 10, 42 },
  { 48, 16, 56, 24, 50, 18, 58, 26 },
  { 12, 44, 4, 36, 14, 46, 6, 38 },
  { 60, 28, 52, 20, 62, 30, 54, 22 },
  { 3, 35, 11, 43, 1, 33, 9, 41 },
  { 51, 19, 59, 27, 49, 17, 57, 25 },
  { 15, 47, 7, 39, 13, 45, 5, 37 }
};

void set_multiplier(double multiplier) {
  s_multiplier = multiplier;
}

static int chart_y(double value, double minimum, double maximum, int top, int height) {
  if (maximum <= minimum) {
    return top + height / 2;
  }
  double fraction = (value - minimum) / (maximum - minimum);
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  return top + height - (int)(fraction * height * s_multiplier);
}

// Per-pixel ordered dithering avoids visible vertical two-pixel columns.
// A soft, travelling brightness band crosses the filled area every 2 seconds.
static void draw_dithered_segment(GContext *ctx, GPoint start, GPoint end,
                                  int baseline, int left, int width) {
  if (end.x <= start.x || width <= 0) return;

  graphics_context_set_stroke_color(ctx, CHARTCOLOR);
  const int pulse_center = left - 32 + ((width + 64) * s_pulse_step) / 39;
  const int pulse_radius = 30;

  for (int x = start.x; x < end.x; ++x) {
    int y = start.y + (int)((int32_t)(end.y - start.y) * (x - start.x) /
                             (end.x - start.x));
    if (y >= baseline) continue;

    int distance = x - pulse_center;
    if (distance < 0) distance = -distance;
    int boost = distance < pulse_radius
                  ? ((pulse_radius - distance) * 32) / pulse_radius : 0;
    // Constant base density, variable pulse density; no vertical stripe columns.
    int density = 19 + boost;

    for (int py = y + 2; py < baseline; ++py) {
      if (DITHER_8X8[py & 7][x & 7] < density) {
        graphics_draw_pixel(ctx, GPoint(x, py));
      }
    }
  }
}

void draw_chart_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int num_days = get_num_of_days();
  const int left = CHART_LEFT;
  const int right = bounds.size.w - CHART_RIGHT;
  const int top = CHART_TOP;
  const int baseline = bounds.size.h - CHART_BOTTOM;
  const int height = baseline - top;
  const int width = right - left;

  if (num_days <= 0 || height <= 0 || width <= 0) return;

  graphics_context_set_stroke_color(ctx, FOREGROUND);
  graphics_context_set_text_color(ctx, FOREGROUND);
  graphics_draw_line(ctx, GPoint(left, baseline), GPoint(right, baseline));

  int weekday = get_week_day();
  for (int i = num_days - 1; i >= 0; --i) {
    bool weekly = (i == num_days - 1) ||
                  (weekday == (is_monday_first() ? 0 : 6));
    int x = left + (int)((int32_t)width * i / (num_days - 1 > 0 ? num_days - 1 : 1));
    graphics_draw_line(ctx, GPoint(x, baseline),
                       GPoint(x, baseline + (weekly ? TICK_LONG : TICK_SHORT)));
    if (weekly && i < num_days - 1) {
      graphics_draw_text(ctx, is_monday_first() ? "M" : "S",
                         fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(x - 8, baseline + 4, 16, 17),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    weekday = (weekday + 6) % 7;
  }
  graphics_draw_text(ctx, "T", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(right - 8, baseline + 4, 16, 17),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  bool found = false;
  double min_weight = 0;
  double max_weight = 0;
  for (int i = 0; i < num_days; ++i) {
    if (!get_measured(i)) continue;
    double value = to_current_unit(get_weight(i));
    if (!found) {
      min_weight = max_weight = value;
      found = true;
    } else {
      if (value < min_weight) min_weight = value;
      if (value > max_weight) max_weight = value;
    }
  }
  const bool demo = FORCE_DEMO_CHART || !found;
  if (demo) {
    // Render sample points without touching saved measurements or the details list.
    min_weight = max_weight = to_current_unit(DEMO_WEIGHTS[0]);
    for (int i = 1; i < num_days && i < 15; ++i) {
      double value = to_current_unit(DEMO_WEIGHTS[i]);
      if (value < min_weight) min_weight = value;
      if (value > max_weight) max_weight = value;
    }
    graphics_draw_text(ctx, "DEMO", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(right - 48, top, 46, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  // Padding keeps flat or near-flat weight histories legible.
  double spread = max_weight - min_weight;
  double pad = spread > 0 ? spread * 0.2 : 1.0;
  min_weight -= pad;
  max_weight += pad;

  GPoint points[32];
  int count = 0;
  for (int i = 0; i < num_days && count < 32; ++i) {
    if (!demo && !get_measured(i)) continue;
    if (demo && i >= 15) continue;
    int x = left + (int)((int32_t)width * i / (num_days - 1 > 0 ? num_days - 1 : 1));
    points[count++] = GPoint(x, chart_y(to_current_unit(demo ? DEMO_WEIGHTS[i] : get_weight(i)),
                                         min_weight, max_weight, top, height));
  }
  if (count == 0) return;

  // Dither FIRST so the continuous curve remains crisp above the fill.
  for (int i = 1; i < count; ++i) {
    draw_dithered_segment(ctx, points[i - 1], points[i], baseline, left, width);
  }

  graphics_context_set_stroke_color(ctx, CHARTCOLOR);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 1; i < count; ++i) {
    graphics_draw_line(ctx, points[i - 1], points[i]);
  }
  graphics_context_set_fill_color(ctx, CHARTCOLOR);
  for (int i = 0; i < count; ++i) {
    graphics_fill_circle(ctx, points[i], 2);
  }
}
