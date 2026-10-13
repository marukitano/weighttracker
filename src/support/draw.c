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
// Reserve 48px for rotated DD.MM. plus 12px gap above the ring.
#define CHART_TOP 52
#define CHART_RIGHT 0
#define CHART_BOTTOM 69

static double s_multiplier = 1.0;
static uint8_t s_pulse_step = 0;
static int s_selected_history_offset = 0;

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
  int bottom = marker.y - 12;
  if (bottom < total_height) bottom = total_height;
  graphics_context_set_fill_color(ctx, GColorWhite);
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

void draw_chart_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int left = CHART_LEFT;
  const int width = bounds.size.w - 1 - CHART_RIGHT - left;
  const int top = CHART_TOP;
  const int height = bounds.size.h - CHART_BOTTOM - top;
  const int fill_bottom = bounds.size.h - 1;
  const int range = get_chart_range();
  const bool by_days = get_chart_mode() == 1;
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
  for (int i = 0; i < count; ++i) {
    int x = left + (int)((int32_t)width * i / (count - 1));
    points[i] = GPoint(x, chart_y(values[i], min_weight, max_weight, top, height));
  }

  for (int i = 1; i < count; ++i)
    draw_dithered_segment(ctx, points[i - 1], points[i], fill_bottom, left, width);

  graphics_context_set_stroke_color(ctx, CHARTCOLOR);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 1; i < count; ++i)
    graphics_draw_line(ctx, points[i - 1], points[i]);

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
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, marker, 4);
}
