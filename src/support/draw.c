#include <pebble.h>
#include <math.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/draw.h"
#include "../support/data.h"
#include "../support/parameters.h"
#include "../support/chart_prefs.h"

// Time 2 chart: the retro Bayer dither is adapted from OpenLibreLinkUp.
// The fill grows denser from left (older) to right (newer), with no min/max labels.
#define CHART_LEFT 0
// Reserve 48px for rotated DD.MM. plus 12px gap above the ring.
#define CHART_TOP 52
#define CHART_RIGHT 0
#define CHART_BOTTOM 69

static double s_multiplier = 1.0;
static uint8_t s_pulse_step = 0;
static int s_selected_history_offset = 0;
static bool s_color_scale_ready = false;
static double s_color_min, s_color_max;
static int s_color_top, s_color_height;


void set_chart_selected_history_offset(int offset) {
  s_selected_history_offset = offset < 0 ? 0 : offset;
}

void set_chart_pulse_step(uint8_t step) {
  s_pulse_step = step % 40;
}

// Browse only real, saved measurements in reverse chronological order.
// Browse the measurements visible in the selected chart mode.
static int visible_offset(int offset) {
  if (get_chart_mode() == 0) return offset;
  int seen = 0;
  uint32_t today = get_current_date();
  for (int i = 0; i < get_history_count(); ++i) {
    uint32_t date = get_history_date(i);
    long age = days(date, today);
    if (age < 0 || age >= get_chart_range()) continue;
    if (seen++ == offset) return i;
  }
  return -1;
}
double get_chart_history_weight(int offset) {
  int i = visible_offset(offset);
  return i < 0 ? -1 : get_history_weight(i);
}
int get_chart_history_count(void) {
  if (get_chart_mode() == 0) {
    int count = get_history_count();
    return count < get_chart_range() ? count : get_chart_range();
  }
  int count = 0;
  for (int i = 0; i < get_history_count(); ++i) {
    long age = days(get_history_date(i), get_current_date());
    if (age >= 0 && age < get_chart_range()) ++count;
  }
  return count;
}
double get_chart_display_weight(void) {
  return get_latest_weight();
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

// Shape-preserving cubic interpolation: smooth at readings, with no
// overshoot between consecutive measurements.
static int smooth_curve_y(const GPoint *p, int count, int seg, int x) {
  GPoint a = p[seg], b = p[seg + 1];
  int dx = b.x - a.x;
  if (dx <= 0) return a.y;
  double slope = (double)(b.y - a.y) / dx;
  double m0 = slope, m1 = slope;
  if (seg > 0) {
    int prev_dx = a.x - p[seg - 1].x;
    if (prev_dx > 0) {
      double prev = (double)(a.y - p[seg - 1].y) / prev_dx;
      m0 = prev * slope > 0 ? 2.0 * prev * slope / (prev + slope) : 0;
    }
  }
  if (seg + 2 < count) {
    int next_dx = p[seg + 2].x - b.x;
    if (next_dx > 0) {
      double next = (double)(p[seg + 2].y - b.y) / next_dx;
      m1 = next * slope > 0 ? 2.0 * next * slope / (next + slope) : 0;
    }
  }
  double t = (double)(x - a.x) / dx;
  double t2 = t * t, t3 = t2 * t;
  double y = (2*t3 - 3*t2 + 1)*a.y + (t3 - 2*t2 + t)*dx*m0
           + (-2*t3 + 3*t2)*b.y + (t3 - t2)*dx*m1;
  int low = a.y < b.y ? a.y : b.y;
  int high = a.y > b.y ? a.y : b.y;
  if (y < low) y = low;
  if (y > high) y = high;
  return (int)(y + 0.5);
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

// 3x5 pixel characters, rotated 90 degrees as one DD.MM. label.
// Each source pixel is enlarged to 2x2 display pixels for readability.
static const uint8_t DATE_GLYPHS[11][5] = {
  {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
  {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
  {7,5,7,5,7}, {7,5,7,1,7}, {0,0,0,2,2}
};

static void draw_vertical_date(GContext *ctx, const char *label,
                               GPoint marker, GRect bounds) {
  const int pixel = 2;
  const int total_height = 6 * 4 * pixel; // DD.MM. has six characters.
  int x0 = marker.x - 5;
  if (x0 < 0) x0 = 0;
  if (x0 > bounds.size.w - 10) x0 = bounds.size.w - 10;
  int bottom = marker.y - 14;
  if (bottom < total_height) bottom = total_height;
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int c = 0; label[c] && c < 6; ++c) {
    int glyph = label[c] == '.' ? 10 : label[c] - '0';
    if (glyph < 0 || glyph > 10) continue;
    for (int row = 0; row < 5; ++row) {
      for (int col = 0; col < 3; ++col) {
        if (!(DATE_GLYPHS[glyph][row] & (1 << (2 - col)))) continue;
        int x = x0 + row * pixel;
        int y = bottom - (c * 4 + col + 1) * pixel;
        graphics_fill_rect(ctx, GRect(x, y, pixel, pixel), 0, GCornerNone);
      }
    }
  }
}

// The color regions are defined by weight thresholds, not by individual
// measurements. The smooth curve crosses them at the real intersection.
static int bmi_band(double display_weight) {
  int height = chart_prefs_height_cm();
  if (height < 100) return 2;
  double meters = height / 100.0;
  double bmi = from_current_unit(display_weight) / (meters * meters);
  if (bmi < 18.5) return 1; // underweight: yellow
  if (bmi < 25.0) return 0; // healthy range: green
  if (bmi < 30.0) return 1; // overweight: yellow
  return 2;                // obesity: red
}
static GColor band_fill(int band) {
  if (band == 0) return GColorDarkGreen;
  if (band == 1) return GColorWindsorTan;
  return GColorDarkCandyAppleRed;
}
static GColor band_stroke(int band) {
  if (band == 0) return GColorGreen;
  if (band == 1) return GColorYellow;
  return GColorRed;
}
static double weight_at_y(int y, double min, double max, int top, int height) {
  return max - ((double)(y - top) / height) * (max - min);
}

GColor chart_outline_color_at_y(int screen_y) {
  if (chart_prefs_color_mode() != 0 || chart_prefs_height_cm() < 100 ||
      !s_color_scale_ready)
    return chart_prefs_line_color();
  double weight = weight_at_y(screen_y, s_color_min, s_color_max,
                              s_color_top, s_color_height);
  return band_stroke(bmi_band(weight));
}

void draw_chart_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int left = CHART_LEFT;
  const int width = bounds.size.w - 1 - CHART_RIGHT - left;
  const int top = bounds.size.h / 3;
  const int height = bounds.size.h / 3;
  const int range = get_chart_range();
  const bool by_days = get_chart_mode() == 1;
  s_color_scale_ready = false;
  if (width < 1 || height < 1) return;

  GPoint points[32];
  bool measured[32] = {false};
  int count = 0;
  int selected = 0;
  double values[32];
  double min_weight = 0, max_weight = 0;
  bool found = false;

  if (!by_days) {
    // Evenly spaced measurements, independent of gaps in calendar time.
    int available = get_history_count();
    int take = available < range ? available : range;
    if (take < 1) return;
    // When there are fewer than 2 readings, extend the flat curve edge to edge.
    count = take < 2 ? 2 : take;
    for (int i = 0; i < count; ++i) {
      int newest_offset = take == 1 ? 0 : take - 1 - i;
      values[i] = to_current_unit(get_history_weight(newest_offset));
      measured[i] = true;
      if (!found) { min_weight = max_weight = values[i]; found = true; }
      else {
        if (values[i] < min_weight) min_weight = values[i];
        if (values[i] > max_weight) max_weight = values[i];
      }
    }
    selected = take == 1 ? count - 1 : take - 1 - s_selected_history_offset;
    if (selected < 0) selected = 0;
    if (selected >= count) selected = count - 1;
  } else {
    // Calendar mode preserves true horizontal date spacing.
    count = range;
    uint32_t today = get_current_date();
    int first_known = -1;
    for (int i = 0; i < count; ++i) {
      int age = count - 1 - i;
      for (int j = 0; j < get_history_count(); ++j) {
        if (days(get_history_date(j), today) != age) continue;
        values[i] = to_current_unit(get_history_weight(j));
        measured[i] = true;
        if (first_known < 0) first_known = i;
        if (!found) { min_weight = max_weight = values[i]; found = true; }
        else {
          if (values[i] < min_weight) min_weight = values[i];
          if (values[i] > max_weight) max_weight = values[i];
        }
        break;
      }
    }
    if (first_known < 0) return;
    for (int i = 0; i < first_known; ++i) values[i] = values[first_known];
    for (int i = first_known + 1; i < count; ++i)
      if (!measured[i]) values[i] = values[i - 1];
    selected = first_known;
    int seen = 0;
    for (int i = count - 1; i >= 0; --i) {
      if (!measured[i]) continue;
      if (seen++ == s_selected_history_offset) { selected = i; break; }
    }
  }

  double spread = max_weight - min_weight;
  double pad = spread > 0 ? spread * 0.2 : 1.0;
  min_weight -= pad;
  max_weight += pad;
  s_color_min = min_weight;
  s_color_max = max_weight;
  s_color_top = top;
  s_color_height = height;
  s_color_scale_ready = true;
  for (int i = 0; i < count; ++i) {
    int x = left + (int)((int32_t)width * i / (count - 1));
    points[i] = GPoint(x, chart_y(values[i], min_weight, max_weight, top, height));
  }

  // White canvas, no area fill. Only the smoothed weight line is drawn
  // within the middle third of the display.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  int segment = 0;
  int previous_y = points[0].y;
  for (int x = points[0].x; x <= points[count - 1].x; ++x) {
    while (segment + 2 < count && x > points[segment + 1].x) ++segment;
    int y = smooth_curve_y(points, count, segment, x);
    if (x > points[0].x) {
      // Split line at every vertical BMI color transition (when enabled).
      graphics_context_set_stroke_color(ctx, GColorRed);
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_line(ctx, GPoint(x - 1, previous_y), GPoint(x, y));
    }
    previous_y = y;
  }

  // Selected real measurement: thicker red ring with black center.
  GPoint marker = points[selected];

  // Rotated DD.MM. date for older selections only. Pixel font is drawn
  // directly at 90 degrees, avoiding an expensive rotated bitmap.
  if (s_selected_history_offset > 0) {
    int history_offset = s_selected_history_offset;
    if (by_days) {
      uint32_t today = get_current_date();
      int seen = 0;
      history_offset = -1;
      for (int i = 0; i < get_history_count(); ++i) {
        long age = days(get_history_date(i), today);
        if (age < 0 || age >= range) continue;
        if (seen++ == s_selected_history_offset) {
          history_offset = i;
          break;
        }
      }
    }
    if (history_offset >= 0 && history_offset < get_history_count()) {
      uint32_t date = get_history_date(history_offset);
      char short_date[8];
      snprintf(short_date, sizeof(short_date), "%02lu.%02lu.",
               (unsigned long)(date % 100),
               (unsigned long)((date / 100) % 100));
      draw_vertical_date(ctx, short_date, marker, bounds);
    }
  }
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, marker, 8);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, marker, 4);
}
