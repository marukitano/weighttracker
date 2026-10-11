#include <pebble.h>
#include <math.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/draw.h"
#include "../support/data.h"
#include "../support/parameters.h"

// Time 2 chart: the retro Bayer dither is adapted from OpenLibreLinkUp.
// The fill grows denser from left (older) to right (newer), with no min/max labels.
#define CHART_LEFT 0
#define CHART_TOP -8
#define CHART_RIGHT 0
#define CHART_BOTTOM 79

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

// Match the displayed header to the temporary preview curve.
double get_chart_display_weight(void) {
#if FORCE_DEMO_CHART
  return DEMO_WEIGHTS[14];
#else
  return get_latest_weight();
#endif
}


static const uint8_t DITHER_8X8[8][8] = {
  { 0, 32, 8, 40, 2, 34, 10, 42 },
  { 48, 16, 56, 24, 50, 18, 58, 26 },
  { 12, 44, 4, 36, 14, 46, 6, 38 },
  { 60, 28, 52, 20, 62, 30, 54, 22 },
  { 3, 35, 11, 43, 1, 33, 9, 41 },
  { 51, 19, 59, 27, 49, 17, 57, 25 },
  { 15, 47, 7, 39, 13, 45, 5, 37 },
  { 63, 31, 55, 23, 61, 29, 53, 21 }
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

// Same 2x2-pixel Bayer dither and density formula as OpenLibreLinkUp.
// Pulse travels from right to left without changing the underlying gradient.
static void draw_dithered_segment(GContext *ctx, GPoint start, GPoint end,
                                  int baseline, int left, int width) {
  if (end.x <= start.x || width <= 0) return;

  // Spread the original Bayer gradient over the entire plot width.
  // No empty first quarter and no early fully-solid final third.
  int fill_start_x = left;
  int fill_width = width;
  int solid_start = fill_width;
  int pulse_center = left + width + 32 - ((width + 64) * s_pulse_step) / 39;

  graphics_context_set_fill_color(ctx, CHARTCOLOR);

  for (int x = start.x; x <= end.x; ++x) {
    if (x < fill_start_x || ((x - fill_start_x) & 1) != 0) continue;

    int y = start.y + (int)((int32_t)(end.y - start.y) * (x - start.x) /
                             (end.x - start.x));
    int relative_x = x - fill_start_x;
    int density = 1;
    if (relative_x >= solid_start) {
      density = 64;
    } else if (solid_start > 0) {
      density = 1 + (relative_x * 63) / solid_start;
    }
    if (density > 64) density = 64;

    // A travelling, gently brighter wave, retaining the original density fade.
    int distance = x - pulse_center;
    if (distance < 0) distance = -distance;
    if (distance < 34) {
      int pulse = ((34 - distance) * 36) / 34;
      density += (pulse * (64 - density)) / 48;
    }

    for (int fill_y = y + 1; fill_y <= baseline; fill_y += 2) {
      int logical_x = (x - fill_start_x) / 2;
      int logical_y = fill_y / 2;
      uint8_t threshold = DITHER_8X8[logical_y & 7][logical_x & 7];
      if (threshold < density) {
        graphics_fill_rect(ctx, GRect(x, fill_y, 2, 2), 0, GCornerNone);
      }
    }
  }
}

void draw_chart_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int num_days = get_chart_days();
  const int total_days = get_num_of_days();
  const int first_day = total_days - num_days;
  const int left = CHART_LEFT;
  const int right = bounds.size.w - 1 - CHART_RIGHT;
  const int top = CHART_TOP;
  const int baseline = bounds.size.h - CHART_BOTTOM;
  const int fill_bottom = bounds.size.h - 1;
  // Fixed plot height: always map the recorded weight range into this space.
  const int height = baseline - top;
  const int width = right - left;

  if (num_days <= 0 || height <= 0 || width <= 0) return;

  bool found = false;
  double min_weight = 0;
  double max_weight = 0;
  for (int i = 0; i < num_days; ++i) {
    if (!get_measured(first_day + i)) continue;
    double value = to_current_unit(get_weight(first_day + i));
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
    for (int i = 1; i < 15; ++i) {
      double value = to_current_unit(DEMO_WEIGHTS[i]);
      if (value < min_weight) min_weight = value;
      if (value > max_weight) max_weight = value;
    }
  }

  // Padding keeps flat or near-flat weight histories legible.
  double spread = max_weight - min_weight;
  double pad = spread > 0 ? spread * 0.2 : 1.0;
  min_weight -= pad;
  max_weight += pad;

  GPoint points[32];
  int count = 0;
  for (int i = 0; i < num_days && count < 32; ++i) {
    if (!demo && !get_measured(first_day + i)) continue;
    
    int x = left + (int)((int32_t)width * i / (num_days - 1 > 0 ? num_days - 1 : 1));
    points[count++] = GPoint(x, chart_y(to_current_unit(demo ? DEMO_WEIGHTS[(i * 14) / (num_days - 1)] : get_weight(first_day + i)),
                                         min_weight, max_weight, top, height));
  }
  if (count == 0) return;

  // Dither FIRST so the continuous curve remains crisp above the fill.
  for (int i = 1; i < count; ++i) {
    draw_dithered_segment(ctx, points[i - 1], points[i], fill_bottom, left, width);
  }

  graphics_context_set_stroke_color(ctx, CHARTCOLOR);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 1; i < count; ++i) {
    graphics_draw_line(ctx, points[i - 1], points[i]);
  }
}
