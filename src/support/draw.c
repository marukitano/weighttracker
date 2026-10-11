#include <pebble.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/draw.h"
#include "../support/data.h"
#include "../support/parameters.h"

#define PATTERN1_LENGTH 6
#define PATTERN2_LENGTH 4
#define DIVIDER_COUNT 7

// Chart geometry is calculated from the chart layer, not a fixed 144x110 box.
// Original layout: origin (28,9), plotting area 100x70.
static int s_origo_x = 28;
static int s_origo_y = 9;
static int s_plot_height = 70;
static int s_plot_width = 100;
#define ORIGO_X s_origo_x
#define ORIGO_Y s_origo_y
#define HEIGHT s_plot_height
#define WIDTH s_plot_width
#define RIGHT_MARGIN 7
#define ARROW_LENGTH 5
#define ARROW_WIDTH 2
#define MARKER_LENGTH 2
#define LONG_MARKER_LENGTH 4

static double s_multiplier = 0;

static const bool PATTERN1[PATTERN1_LENGTH] = { 0, 0, 0, 1, 1, 1 };
static const bool PATTERN2[PATTERN2_LENGTH] = { 0, 0, 0, 1 };

static const int DIVIDERS[DIVIDER_COUNT] = {1, 2, 5, 10, 20, 50, 100};

void set_multiplier(double m) {
  s_multiplier = m;
}

int calculate_divider(int min, int max) {
  int i, j, d, lower, upper;
  int diff = max - min;

  for (i = 0; i < DIVIDER_COUNT; i++) {
    for (j = 2; j < 4; j++) {
      if (diff < j * DIVIDERS[i]) {
        d = DIVIDERS[i];
        lower = (min / d) * d;
        upper = (max / d) * d;

        if (upper < max) {
          upper++;
        }

        if ((upper - lower) / d < 4) {
          return d;
        }
      }
    }
  }

  return DIVIDERS[DIVIDER_COUNT - 1];
}

void draw_horizontal_pattern(GContext *ctx, const int start, const int end, const int y, const bool* pattern, const int plen, GColor stroke) {
  int i;

  graphics_context_set_stroke_color(ctx, stroke);

  for (i = start; i <= end; i++) {
    if (pattern[i % plen]) {
      graphics_draw_pixel(ctx, GPoint(i, y));
    }
  }
}

void draw_axis(GContext *ctx, int lower, int upper, int mark, double step) {
  int i, y;
  static char buffer[] = "100";
  GRect frame = GRect(0, 0, 23, 16);

  graphics_context_set_stroke_color(ctx, FOREGROUND);
  graphics_context_set_text_color(ctx, FOREGROUND);

  for (i = lower; i <= upper; i += mark) {
    y = ORIGO_Y + HEIGHT - (int)(step * (double)(i - lower));
    frame.origin.y = y - 9;
    snprintf(buffer, sizeof("100"), "%d", i);

    graphics_draw_text(ctx,
      buffer,
      fonts_get_system_font(FONT_KEY_GOTHIC_14),
      frame,
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentRight,
      NULL
    );

    graphics_draw_line(ctx, GPoint(ORIGO_X - MARKER_LENGTH, y), GPoint(ORIGO_X, y));

    if (i > lower) {
      draw_horizontal_pattern(ctx, ORIGO_X, ORIGO_X + WIDTH + RIGHT_MARGIN, y, PATTERN2, PATTERN2_LENGTH, FOREGROUND);
    }
  }
}

void draw_chart_callback(Layer *layer, GContext *ctx) {
  int i, imin, imax, y, drawflag, mark, lower, upper;
  int cwd;
  bool longtick;
  double div, min, max, step, w;
  GPoint sp, ep;

  const GRect bounds = layer_get_bounds(layer);
  s_plot_width = bounds.size.w - ORIGO_X - RIGHT_MARGIN - 9;
  s_plot_height = bounds.size.h - ORIGO_Y - 31;

  const int num_of_days = get_num_of_days();

  div = (double)WIDTH / (double)num_of_days;

  graphics_context_set_stroke_color(ctx, FOREGROUND);
  graphics_context_set_fill_color(ctx, FOREGROUND);
  graphics_context_set_text_color(ctx, FOREGROUND);

  // draw axis
  graphics_draw_line(ctx, GPoint(ORIGO_X, 0), GPoint(ORIGO_X, ORIGO_Y + HEIGHT));
  graphics_draw_line(ctx, GPoint(ORIGO_X, ORIGO_Y + HEIGHT), GPoint(ORIGO_X + WIDTH + RIGHT_MARGIN, ORIGO_Y + HEIGHT));

  // draw tickmarks on horizontal axis
  graphics_draw_line(ctx, GPoint(ORIGO_X, ORIGO_Y + HEIGHT), GPoint(ORIGO_X, ORIGO_Y + HEIGHT + MARKER_LENGTH));

  cwd = get_week_day();

  for (i = num_of_days - 1; i >= 0; i--) {
    longtick = (i == num_of_days - 1) || (cwd == (is_monday_first() ? 0 : 6)); // MONDAY = 0, SUNDAY = 6

    graphics_draw_line(ctx,
      GPoint(ORIGO_X + (int)(div * i), ORIGO_Y + HEIGHT),
      GPoint(ORIGO_X + (int)(div * i), ORIGO_Y + HEIGHT + (longtick ? LONG_MARKER_LENGTH : MARKER_LENGTH)));

    if (longtick && i < num_of_days - 1) {
      graphics_draw_text(ctx,
        (is_monday_first() ? "M" : "S"),
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(ORIGO_X + (int)(div * i) - 7, ORIGO_Y + HEIGHT + LONG_MARKER_LENGTH, 16, 16),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter,
        NULL
      );
    }

    cwd = (cwd + 6) % 7;
  }

  graphics_draw_text(ctx,
    "T",
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(ORIGO_X + WIDTH - (int)div - 8, ORIGO_Y + HEIGHT + LONG_MARKER_LENGTH, 16, 16),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );

  // draw arrow heads
  graphics_draw_line(ctx, GPoint(ORIGO_X, 0), GPoint(ORIGO_X - ARROW_WIDTH, ARROW_LENGTH));
  graphics_draw_line(ctx, GPoint(ORIGO_X, 0), GPoint(ORIGO_X + ARROW_WIDTH, ARROW_LENGTH));

  // draw arrow heads
  graphics_draw_line(ctx, GPoint(ORIGO_X + WIDTH + RIGHT_MARGIN, ORIGO_Y + HEIGHT), GPoint(ORIGO_X + WIDTH + RIGHT_MARGIN - ARROW_LENGTH, ORIGO_Y + HEIGHT - ARROW_WIDTH));
  graphics_draw_line(ctx, GPoint(ORIGO_X + WIDTH + RIGHT_MARGIN, ORIGO_Y + HEIGHT), GPoint(ORIGO_X + WIDTH + RIGHT_MARGIN - ARROW_LENGTH, ORIGO_Y + HEIGHT + ARROW_WIDTH));

 // calculate and draw axis
  min = to_current_unit(get_latest_weight());
  max = to_current_unit(get_latest_weight());

  for (i = 0; i < num_of_days; i++) {
    w = to_current_unit(get_weight(i));

    if (get_measured(i)) {
      if (min < 0.0 || min > w) {
        min = w;
      }

      if (max < 0.0 || max < w) {
        max = w;
      }
    }
  }

  max = ceil(max);
  min = floor(min);

  if (min == max) {
    min = min - 1;
  }

  imax = (int)max;
  imin = (int)min;

  mark = calculate_divider(imin, imax);
  lower = (imin / mark) * mark;
  upper = (imax / mark) * mark;

  if (upper < imax) {
    upper += mark;
  }

  step = (double)HEIGHT / (upper - lower);

  draw_axis(ctx, lower, upper, mark, step);

  // draw the curve
  graphics_context_set_stroke_color(ctx, CHARTCOLOR);
  graphics_context_set_fill_color(ctx, CHARTCOLOR);

  drawflag = 0;

  w = -1.0;

  if (get_measured(0)) {
    w = to_current_unit(get_weight(0));
    sp = GPoint(ORIGO_X, ORIGO_Y + HEIGHT - (int)((w - (double)lower) * step * s_multiplier));
    graphics_fill_circle(ctx, sp, 2);
    drawflag = 1;
  }

  for (i = 1; i < num_of_days; i++) {
    if (get_measured(i)) {
      w = to_current_unit(get_weight(i));
      ep = GPoint(ORIGO_X + (int)(div * i), ORIGO_Y + HEIGHT - (int)((w - (double)lower) * step * s_multiplier));

      if (drawflag) {
        graphics_draw_line(ctx, sp, ep);
      } else {
        draw_horizontal_pattern(ctx, ORIGO_X, ep.x, ep.y, PATTERN1, PATTERN1_LENGTH, CHARTCOLOR);
        drawflag = 1;
      }

      graphics_fill_circle(ctx, ep, 2);

      sp = ep;
    }
  }

  if (drawflag) {
    y = ORIGO_Y + HEIGHT - (int)((w - (double)lower) * step * s_multiplier);
    draw_horizontal_pattern(ctx, sp.x, ORIGO_X + WIDTH + RIGHT_MARGIN, y, PATTERN1, PATTERN1_LENGTH, CHARTCOLOR);
  }
}
