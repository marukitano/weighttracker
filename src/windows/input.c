#include <pebble.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/data.h"
#include "../support/parameters.h"
#include "input.h"

// Touch ruler based on the vertical selection interaction in Swiss Chronograph.
// Internally values are integer tenths in the currently selected unit.
#define STEP_PX 12
#define VALUE_MIN 1
#define VALUE_MAX 50000
static Window *s_window;
static Layer *s_canvas;
static int s_edit_offset = -1;
static int s_tenths;
static int s_drag_pixels;
static bool s_touching;
static int s_last_y;
static bool s_confirm_delete;
static bool s_touch_registered;
static AppTimer *s_roll_timer;
static int s_roll_offset;
static AppTimer *s_fling_timer;
static int32_t s_velocity_q8;
static int32_t s_fling_fraction_q8;

static int clamp_value(int value) {
  if (value < VALUE_MIN) return VALUE_MIN;
  if (value > VALUE_MAX) return VALUE_MAX;
  return value;
}

static void refresh(void) {
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void roll_tick(void *context) {
  s_roll_timer = NULL;
  if (s_roll_offset == 0) return;
  int d = -s_roll_offset * 48 / 100;
  if (!d) d = s_roll_offset > 0 ? -1 : 1;
  s_roll_offset += d;
  if (s_roll_offset > -2 && s_roll_offset < 2) s_roll_offset = 0;
  refresh();
  if (s_roll_offset)
    s_roll_timer = app_timer_register(16, roll_tick, NULL);
}

static void apply_drag(int pixels) {
  s_drag_pixels += pixels;
  while (s_drag_pixels >= STEP_PX && s_tenths < VALUE_MAX) {
    ++s_tenths;
    s_drag_pixels -= STEP_PX;
  }
  while (s_drag_pixels <= -STEP_PX && s_tenths > VALUE_MIN) {
    --s_tenths;
    s_drag_pixels += STEP_PX;
  }
  if ((s_tenths == VALUE_MAX && s_drag_pixels > 0) ||
      (s_tenths == VALUE_MIN && s_drag_pixels < 0))
    s_drag_pixels = 0;
  refresh();
}

static void finish_drag(void) {
  s_roll_offset = s_drag_pixels;
  s_drag_pixels = 0;
  if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
  if (s_roll_offset) s_roll_timer = app_timer_register(16, roll_tick, NULL);
  refresh();
}

static void fling_tick(void *context) {
  s_fling_timer = NULL;
  s_fling_fraction_q8 += s_velocity_q8;
  int movement = (int)(s_fling_fraction_q8 / 256);
  s_fling_fraction_q8 -= movement * 256;
  if (movement) apply_drag(movement);
  s_velocity_q8 = s_velocity_q8 * 92 / 100;
  if ((s_velocity_q8 > -128 && s_velocity_q8 < 128) ||
      (s_tenths == VALUE_MIN && s_velocity_q8 < 0) ||
      (s_tenths == VALUE_MAX && s_velocity_q8 > 0)) {
    finish_drag();
    return;
  }
  s_fling_timer = app_timer_register(28, fling_tick, NULL);
  if (!s_fling_timer) finish_drag();
}

#if PBL_TOUCH
static void on_touch(const TouchEvent *event, void *context) {
  if (s_confirm_delete) return;
  switch (event->type) {
    case TouchEvent_Touchdown:
      if (s_fling_timer) { app_timer_cancel(s_fling_timer); s_fling_timer = NULL; }
      if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
      s_touching = true;
      s_last_y = event->y;
      s_drag_pixels = s_roll_offset;
      s_roll_offset = 0;
      s_velocity_q8 = 0;
      break;
    case TouchEvent_PositionUpdate:
      if (!s_touching) break;
      {
        int delta = event->y - s_last_y;
        s_last_y = event->y;
        s_velocity_q8 = (s_velocity_q8 + delta * 256 * 3) / 4;
        apply_drag(delta);
      }
      break;
    case TouchEvent_Liftoff:
      s_touching = false;
      if (s_velocity_q8 > 9216) s_velocity_q8 = 9216;
      if (s_velocity_q8 < -9216) s_velocity_q8 = -9216;
      s_fling_fraction_q8 = 0;
      if (s_velocity_q8 > 1024 || s_velocity_q8 < -1024) {
        s_fling_timer = app_timer_register(28, fling_tick, NULL);
        if (!s_fling_timer) finish_drag();
      } else finish_drag();
      break;
    default: break;
  }
}
#endif

// Same custom 5x7 pixel glyphs as the rotated chart labels.
static const uint8_t s_weight_glyphs[11][7] = {
  {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
  {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
  {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
  {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
  {0,0,0,0,0,12,12}
};

static void draw_pixel_weight(GContext *ctx, const char *value, int width) {
  const int scale = 4;
  int count = 0, pixels = 0;
  while (value[count] && count < 20) {
    pixels += value[count] == '.' ? 4 : 6;
    ++count;
  }
  int x = (width - pixels * scale) / 2;
  const int y = 9;
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int c = 0; c < count; ++c) {
    int glyph = value[c] == '.' ? 10 : value[c] - '0';
    if (glyph < 0 || glyph > 10) continue;
    for (int row = 0; row < 7; ++row)
      for (int col = 0; col < 5; ++col)
        if (s_weight_glyphs[glyph][row] & (1 << (4 - col)))
          graphics_fill_rect(ctx,
            GRect(x + col * scale, y + row * scale, scale, scale),
            0, GCornerNone);
    x += (value[c] == '.' ? 4 : 6) * scale;
  }
}

static void draw_canvas(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int ruler_x = 0;
  const int readout_y = 29;  // Vertical center of the top weight readout.
  int cy = bounds.size.h / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);

  if (s_confirm_delete) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(9, cy - 45, bounds.size.w - 18, 90),
                       8, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "Delete reading?",
       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
       GRect(10, cy - 38, bounds.size.w - 20, 34),
       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "Back: delete  |  Up: cancel",
       fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
       GRect(10, cy + 4, bounds.size.w - 20, 30),
       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // The marker stays still while the ruler ticks roll beneath it.
  for (int step = -22; step <= 22; ++step) {
    int value = s_tenths + step;
    if (value <= 0) continue;
    int y = readout_y - step * STEP_PX + (s_touching || s_fling_timer ? s_drag_pixels : s_roll_offset);
    if (y < 0 || y >= bounds.size.h) continue;
    bool major = value % 10 == 0;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, major ? 2 : 1);
    graphics_draw_line(ctx, GPoint(ruler_x, y),
                            GPoint(ruler_x + (major ? 16 : 9), y));
    if (major && step != 0) {
      char tick[16];
      snprintf(tick, sizeof(tick), "%d", value / 10);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, tick, fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(ruler_x + 17, y - 10, 20, 22), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft, NULL);
    }
  }
  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(ruler_x, readout_y), GPoint(ruler_x + 32, readout_y));
  // Swiss Chronograph-style number bubble: a hollow red oval swells
  // around a major tick label as that label approaches the readout line.
  int nearest_value = -1;
  int nearest_y = 0;
  int nearest_distance = STEP_PX;
  int fine = (s_touching || s_fling_timer) ? s_drag_pixels : s_roll_offset;
  for (int step = -12; step <= 12; ++step) {
    int value = s_tenths + step;
    if (value <= 0 || value % 10 != 0) continue;
    int y = readout_y - step * STEP_PX + fine;
    int distance = y - readout_y;
    if (distance < 0) distance = -distance;
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest_value = value;
      nearest_y = y;
    }
  }
  if (nearest_value > 0) {
    char label[16];
    snprintf(label, sizeof(label), "%d", nearest_value / 10);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    GSize text_size = graphics_text_layout_get_content_size(
      label, font, GRect(0, 0, 80, 20),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int progress = (STEP_PX - nearest_distance) * 100 / STEP_PX;
    progress = progress * progress * (300 - 2 * progress) / 10000;
    int target_w = text_size.w + 12;
    if (target_w < 20) target_w = 20;
    int oval_w = 1 + (target_w - 1) * progress / 100;
    int oval_h = 1 + 19 * progress / 100;
    if (oval_h >= 2 && oval_w >= 2) {
      int bubble_cx = 17 + text_size.w / 2;
      GRect bubble = GRect(bubble_cx - oval_w / 2,
                           readout_y - oval_h / 2, oval_w, oval_h);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, bubble, oval_h / 2, GCornersAll);
      graphics_context_set_stroke_color(ctx, GColorRed);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_round_rect(ctx, bubble, oval_h / 2);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, label, font,
        GRect(17, nearest_y - 10, 42, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }
  char weight[24];
  snprintf(weight, sizeof(weight), "%d.%d", s_tenths / 10, s_tenths % 10);
  draw_pixel_weight(ctx, weight, bounds.size.w);

  // Left delete marker appears only when editing.
  if (s_edit_offset >= 0) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(0, 36), 26);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(9, 29), GPoint(21, 43));
    graphics_draw_line(ctx, GPoint(9, 43), GPoint(21, 29));
  }
  // Right save marker.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(bounds.size.w - 25, 10, 25, 52),
                     5, GCornersLeft);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(bounds.size.w - 20, 36),
                          GPoint(bounds.size.w - 15, 42));
  graphics_draw_line(ctx, GPoint(bounds.size.w - 15, 42),
                          GPoint(bounds.size.w - 6, 29));
}

static void save_click(ClickRecognizerRef recognizer, void *context) {
  if (s_confirm_delete) {
    s_confirm_delete = false;
    refresh();
    return;
  }
  double weight = (double)s_tenths / 10.0;
  if (s_edit_offset >= 0)
    update_history_weight(s_edit_offset, from_current_unit(weight));
  else
    set_current(from_current_unit(weight));
  window_stack_pop(true);
}

static void delete_click(ClickRecognizerRef recognizer, void *context) {
  if (s_edit_offset < 0) { window_stack_pop(true); return; }
  if (!s_confirm_delete) {
    s_confirm_delete = true;
    refresh();
    return;
  }
  delete_history_weight(s_edit_offset);
  window_stack_pop(true);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, save_click);
  window_single_click_subscribe(BUTTON_ID_BACK, delete_click);
}

static void on_appear(Window *window) {
#if PBL_TOUCH
  if (touch_service_is_enabled() && !s_touch_registered) {
    touch_service_subscribe(on_touch, NULL);
    s_touch_registered = true;
  }
#endif
}
static void on_disappear(Window *window) {
#if PBL_TOUCH
  if (s_touch_registered) {
    touch_service_unsubscribe();
    s_touch_registered = false;
  }
#endif
}
static void on_unload(Window *window) {
  on_disappear(window);
  if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
  if (s_fling_timer) { app_timer_cancel(s_fling_timer); s_fling_timer = NULL; }
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_canvas = NULL;
  s_window = NULL;
}

static void open_editor(int edit_offset) {
  s_edit_offset = edit_offset;
  s_confirm_delete = false;
  s_drag_pixels = 0;
  s_roll_offset = 0;
  s_touching = false;
  double initial = edit_offset >= 0
    ? get_history_weight(edit_offset) : get_latest_weight();
  if (initial <= 0) initial = 80.0;
  double current_unit = to_current_unit(initial);
  s_tenths = clamp_value((int)(current_unit * 10 + 0.5));
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  Layer *root = window_get_root_layer(s_window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw_canvas);
  layer_add_child(root, s_canvas);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .appear = on_appear,
    .disappear = on_disappear,
    .unload = on_unload
  });
  window_stack_push(s_window, true);
}

void show_input(void) { open_editor(-1); }
void show_input_edit(int newest_offset) {
  if (newest_offset < 0 || newest_offset >= get_history_count()) return;
  open_editor(newest_offset);
}
void hide_input(void) { if (s_window) window_stack_remove(s_window, true); }
