/***
WeightTracker - a Pebble app to track weight loss/gain
Copyright (C) 2026 Viktor Csala

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
***/

// ORIGINAL_UUID "7151ed69-3204-4914-8aaa-860e1494a504"
// TEST_UUID "4d2f7dd6-2b26-4cce-ace5-410741b853bb"

#include <pebble.h>
#include "support/config.h"
#include "support/utils.h"
#include "support/draw.h"
#include "support/data.h"
#include "support/pin.h"
#include "support/parameters.h"
#include "support/chart_prefs.h"
#include "support/sync.h"
#include "windows/input.h"
#include "windows/wakeup.h"
#include "main.h"


static int s_today;

static Window *s_window;
static TextLayer *s_textlayer_1;
static Layer *s_chart;
static Layer *s_button_marker;
static Layer *s_back_marker;
static AppTimer *s_back_expand_timer;
static AppTimer *s_back_press_timer;
static bool s_back_expanded = false;
static uint8_t s_back_width = 5;
static AppTimer *s_button_anim_timer;
static uint8_t s_button_width[3] = {5, 5, 5};
// 36px offscreen -> small overshoot -> settle, with staggered entrance.
static int8_t s_button_enter_x[3] = {30, 30, 30};
static uint8_t s_button_enter_frame[3] = {0, 0, 0};
static bool s_button_entering[3] = {false, false, false};
static AppTimer *s_button_enter_timer;
static AppTimer *s_button_intro_timer;
static bool s_buttons_introduced = false;
// Exact entry offsets and 42 ms cadence from Swiss-Chronograph
// src/c/touch.c (s_control_bounce_offsets / RUN_CONTROLS_BOUNCE_MS).
static const int8_t BUTTON_BOUNCE_X[] = {30, 18, 8, -3, 2, -1, 0};

static int8_t s_active_button = -1;
static bool s_button_held = false;
static AppTimer *s_pulse_timer;
static uint8_t s_pulse_step;
static bool s_main_visible;
static Layer *s_weight_animation_layer;
static AppTimer *s_weight_animation_timer;
static int s_history_offset = 0;
static int s_incoming_offset = 0;
static int s_slide_frame = 0;
static int s_slide_direction = 1;
static bool s_slide_running = false;
static char s_old_weight_text[24];
static char s_new_weight_text[24];

// One 2.5-second sweep every 5 seconds, with a short pause between waves.
#define PULSE_FRAME_MS 65
#define PULSE_STEPS 40
#define PULSE_PERIOD_MS 5000

static void pulse_timer_callback(void *context) {
  s_pulse_timer = NULL;
  if (!s_main_visible) return;

  uint32_t next_delay;
  if (s_pulse_step == PULSE_STEPS - 1) {
    s_pulse_step = 0;
    next_delay = PULSE_PERIOD_MS - (PULSE_STEPS - 1) * PULSE_FRAME_MS;
  } else {
    s_pulse_step++;
    next_delay = PULSE_FRAME_MS;
  }

  set_chart_pulse_step(s_pulse_step);
  if (s_chart) layer_mark_dirty(s_chart);
  s_pulse_timer = app_timer_register(next_delay, pulse_timer_callback, NULL);
}


// Edge-mounted button marker, matching the Swiss Chronograph's
// 5px x 52px rounded-left hardware-button indicator.
// Three Swiss-Chronograph-style button markers aligned with the Time 2 keys.
// Upper: next measurement (>), middle: edit (pencil), lower: new (+).
static void button_marker_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int center_y[3] = {36, bounds.size.h / 2, bounds.size.h - 36};
  const int edge_x = bounds.size.w;
  // No rail or divider: paint only black button tabs and symbols.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);

  for (int i = 0; i < 3; ++i) {
    if (s_button_enter_x[i] >= 30) continue;
    int shift = s_button_enter_x[i];
    // Swiss Chronograph: stretch the tab back to the physical edge.
    int marker_width = s_button_width[i] + (shift > 0 ? shift : 0);
    graphics_fill_rect(ctx,
                       GRect(edge_x - marker_width, center_y[i] - 26,
                             marker_width, 52),
                       5, GCornersLeft);
    const int icon_right = edge_x - 14 + shift;
    const int icon_left = icon_right - 13;
    if (i == 2) {
      // Add a measurement.
      const int cx = (icon_left + icon_right) / 2;
      graphics_draw_line(ctx, GPoint(cx - 6, center_y[i]),
                         GPoint(cx + 6, center_y[i]));
      graphics_draw_line(ctx, GPoint(cx, center_y[i] - 6),
                         GPoint(cx, center_y[i] + 6));
    } else if (i == 1) {
      // Clean, diagonally tilted pencil with a sharply tapered graphite tip.
      const int cx = (icon_left + icon_right) / 2;
      const int cy = center_y[i];
      // Straight, parallel barrel edges and a squared-off eraser end.
      graphics_draw_line(ctx, GPoint(cx - 7, cy + 3),
                         GPoint(cx + 4, cy - 8));
      graphics_draw_line(ctx, GPoint(cx - 3, cy + 7),
                         GPoint(cx + 8, cy - 4));
      graphics_draw_line(ctx, GPoint(cx + 4, cy - 8),
                         GPoint(cx + 8, cy - 4));

      // Pencil tip uses the same 2px stroke as the barrel.
      graphics_draw_line(ctx, GPoint(cx - 7, cy + 3),
                         GPoint(cx - 10, cy + 10));
      graphics_draw_line(ctx, GPoint(cx - 3, cy + 7),
                         GPoint(cx - 10, cy + 10));
      graphics_draw_line(ctx, GPoint(cx - 7, cy + 3),
                         GPoint(cx - 3, cy + 7));
    } else {
      // Upper key: right chevron (newer measurement).
      const int mid_x = (icon_left + icon_right) / 2;
      const int tip_x = mid_x + 4;
      const int arm_x = mid_x - 3;
      graphics_draw_line(ctx, GPoint(arm_x, center_y[i] - 6),
                         GPoint(tip_x, center_y[i]));
      graphics_draw_line(ctx, GPoint(tip_x, center_y[i]),
                         GPoint(arm_x, center_y[i] + 6));
    }
  }
}

// Swiss Chronograph: left-edge action at y=36, 5x52px, right-rounded.
// Mirror the right-hand button bounce so the back action flies in from left.
static void back_expand_tick(void *context) {
  s_back_expand_timer = NULL;
  const int target = s_back_expanded ? 35 : 5;
  if (s_back_width < target) {
    s_back_width += 5;
    if (s_back_width > target) s_back_width = target;
  } else if (s_back_width > target) {
    s_back_width = s_back_width > 10 ? s_back_width - 5 : 5;
    if (s_back_width < target) s_back_width = target;
  }
  if (s_back_marker) layer_mark_dirty(s_back_marker);
  if (s_back_width != target)
    s_back_expand_timer = app_timer_register(35, back_expand_tick, NULL);
}

static void back_expand_set(bool expanded) {
  s_back_expanded = expanded;
  if (s_back_expand_timer) app_timer_cancel(s_back_expand_timer);
  s_back_expand_timer = app_timer_register(1, back_expand_tick, NULL);
}

static void back_marker_update(Layer *layer, GContext *ctx) {
  if (s_button_enter_x[0] >= 30) return;
  int shift = s_button_enter_x[0];
  int width = s_back_width + (shift > 0 ? shift : 0);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 10, width, 52), 5, GCornersRight);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  int cx = 15 - shift, cy = 36;
  if (s_back_expanded) {
    // White X replaces and covers the old left arrow within the extended tab.
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_line(ctx, GPoint(13, cy - 7), GPoint(25, cy + 7));
    graphics_draw_line(ctx, GPoint(13, cy + 7), GPoint(25, cy - 7));
  } else {
    graphics_draw_line(ctx, GPoint(cx + 4, cy - 6), GPoint(cx - 3, cy));
    graphics_draw_line(ctx, GPoint(cx - 3, cy), GPoint(cx + 4, cy + 6));
  }
}

// Two independent weight captions slide through a clipped display window.
static void format_history_weight(int offset, char *out, size_t len) {
  double value = get_chart_history_weight(offset);
  if (value <= 0) {
    snprintf(out, len, "--");
    return;
  }
  char number[16];
  double_str(to_current_unit(value), number, sizeof(number) - 1);
  snprintf(out, len, "%s", number);
}

// Render the mask text with a one-pixel chart-red outline.
// Draw each portion of the numeral outline with the color of the BMI
// band at precisely the same vertical display coordinate.
// Paint the outline in a temporary marker color, then recolor its pixels
// using the exact BMI band at each absolute framebuffer row. Pebble has
// no graphics_context_set_clip_rect() API.
static void draw_outlined_weight(GContext *ctx, const char *label, GFont font,
                                 GRect rect, GRect clip_bounds,
                                 int absolute_top) {
  graphics_context_set_text_color(ctx, GColorMagenta);
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      if (dx == 0 && dy == 0) continue;
      graphics_draw_text(ctx, label, font,
                         GRect(rect.origin.x + dx, rect.origin.y + dy,
                               rect.size.w, rect.size.h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
    }
  }

  GBitmap *framebuffer = graphics_capture_frame_buffer(ctx);
  if (framebuffer) {
    int top = absolute_top < 0 ? 0 : absolute_top;
    int bottom = absolute_top + clip_bounds.size.h;
    GRect fb_bounds = gbitmap_get_bounds(framebuffer);
    if (bottom > fb_bounds.size.h) bottom = fb_bounds.size.h;
    for (int y = top; y < bottom; ++y) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(framebuffer, y);
      GColor color = chart_outline_color_at_y(y);
      int left = row.min_x > 0 ? row.min_x : 0;
      int right = row.max_x < fb_bounds.size.w - 1 ?
                  row.max_x : fb_bounds.size.w - 1;
      for (int x = left; x <= right; ++x) {
        if (row.data[x] == GColorMagenta.argb)
          row.data[x] = color.argb;
      }
    }
    graphics_release_frame_buffer(ctx, framebuffer);
  }
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, label, font, rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void weight_slide_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int height = bounds.size.h;
  const int absolute_top = layer_get_frame(layer).origin.y;
  GFont font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  if (!s_slide_running) {
    draw_outlined_weight(ctx, s_new_weight_text, font, bounds, bounds,
                         absolute_top);
    return;
  }
  int travel = (height * s_slide_frame) / 8;
  draw_outlined_weight(ctx, s_old_weight_text, font,
                       GRect(0, -s_slide_direction * travel, bounds.size.w, height),
                       bounds, absolute_top);
  draw_outlined_weight(ctx, s_new_weight_text, font,
                       GRect(0, s_slide_direction * (height - travel),
                             bounds.size.w, height),
                       bounds, absolute_top);
}

static void slide_tick(void *context) {
  s_weight_animation_timer = NULL;
  if (!s_slide_running || !s_weight_animation_layer) return;
  ++s_slide_frame;
  layer_mark_dirty(s_weight_animation_layer);
  if (s_slide_frame < 8) {
    s_weight_animation_timer = app_timer_register(35, slide_tick, NULL);
  } else {
    s_slide_running = false;
    s_history_offset = s_incoming_offset;
    layer_mark_dirty(s_weight_animation_layer);
  }
}

static void button_enter_tick(void *context) {
  s_button_enter_timer = NULL;
  bool more = false;
  for (int i = 0; i < 3; ++i) {
    if (!s_button_entering[i]) continue;
    uint8_t frame = s_button_enter_frame[i];
    if (frame < sizeof(BUTTON_BOUNCE_X) / sizeof(BUTTON_BOUNCE_X[0]) - 1)
      ++s_button_enter_frame[i];
    s_button_enter_x[i] = BUTTON_BOUNCE_X[s_button_enter_frame[i]];
    if (s_button_enter_frame[i] >= sizeof(BUTTON_BOUNCE_X) / sizeof(BUTTON_BOUNCE_X[0]) - 1)
      s_button_entering[i] = false;
    else more = true;
  }
  if (s_button_marker) layer_mark_dirty(s_button_marker);
  if (s_back_marker) layer_mark_dirty(s_back_marker);
  if (more) s_button_enter_timer = app_timer_register(42, button_enter_tick, NULL);
}

static void button_intro_tick(void *context) {
  s_button_intro_timer = NULL;
  s_buttons_introduced = true;
  for (int i = 0; i < 3; ++i) {
    s_button_entering[i] = true;
    s_button_enter_frame[i] = 0;
  }
  if (!s_button_enter_timer)
    s_button_enter_timer = app_timer_register(42, button_enter_tick, NULL);
}

static void change_history(int direction) {
  // Navigation is immediate, irrespective of any prior button animation.
  int target = s_history_offset + direction;
  if (target < 0 || target >= get_chart_history_count()) return;
  if (s_weight_animation_timer) {
    app_timer_cancel(s_weight_animation_timer);
    s_weight_animation_timer = NULL;
  }
  s_slide_running = false;
  s_history_offset = target;
  s_incoming_offset = target;
  set_chart_selected_history_offset(target);
  format_history_weight(target, s_new_weight_text, sizeof(s_new_weight_text));
  if (s_chart) layer_mark_dirty(s_chart);
  if (s_weight_animation_layer) layer_mark_dirty(s_weight_animation_layer);
}


static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  // The Time 2 (emery) has a 200x228 display. Give its chart the full
  // available width and height while retaining the original layout elsewhere.
  // draw_chart_callback derives its geometry from these layer bounds.
  // s_chart
#ifdef PBL_PLATFORM_EMERY
  s_chart = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
#else
  s_chart = layer_create(GRect(PBL_IF_ROUND_ELSE((bounds.size.w - 144) / 2 - 4, 0), 40, 144, 110));
#endif
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_chart);

  // s_textlayer_1
#ifdef PBL_PLATFORM_EMERY
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - 63, bounds.size.w, 63));
#else
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(38, 20), bounds.size.w, PBL_IF_ROUND_ELSE(38, 24)));
#endif
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, GColorBlack);
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  // Clip the old/new animated weights to the caption area.
  s_weight_animation_layer = layer_create(layer_get_frame(text_layer_get_layer(s_textlayer_1)));
  layer_set_clips(s_weight_animation_layer, true);
  layer_set_update_proc(s_weight_animation_layer, weight_slide_draw);
  layer_add_child(window_get_root_layer(s_window), s_weight_animation_layer);
  // Weight now follows the fixed center marker as a vertical chart label.
  // Keep legacy layer allocated for navigation compatibility, but hide it.
  layer_set_hidden(s_weight_animation_layer, true);

  // Lower right button indicator: center 36px above bottom, as in Swiss Chronograph.
  s_button_marker = layer_create(GRect(bounds.size.w - 36, 0, 36, bounds.size.h));
  layer_set_update_proc(s_button_marker, button_marker_update);
  layer_add_child(window_get_root_layer(s_window), s_button_marker);
  s_back_marker = layer_create(GRect(0, 0, 36, bounds.size.h));
  layer_set_update_proc(s_back_marker, back_marker_update);
  layer_add_child(window_get_root_layer(s_window), s_back_marker);

#ifdef PBL_ROUND
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_1, 2);
#endif
}

static void destroy_ui(void) {
  window_destroy(s_window);
  layer_destroy(s_chart);
  if (s_button_enter_timer) {
    app_timer_cancel(s_button_enter_timer);
    s_button_enter_timer = NULL;
  }
  if (s_button_intro_timer) {
    app_timer_cancel(s_button_intro_timer);
    s_button_intro_timer = NULL;
  }
  if (s_button_anim_timer) {
    app_timer_cancel(s_button_anim_timer);
    s_button_anim_timer = NULL;
  }
  layer_destroy(s_button_marker);
  if (s_back_press_timer) { app_timer_cancel(s_back_press_timer); s_back_press_timer = NULL; }
  if (s_back_expand_timer) { app_timer_cancel(s_back_expand_timer); s_back_expand_timer = NULL; }
  layer_destroy(s_back_marker);
  s_back_marker = NULL;
  if (s_weight_animation_timer) {
    app_timer_cancel(s_weight_animation_timer);
    s_weight_animation_timer = NULL;
  }
  layer_destroy(s_weight_animation_layer);
  text_layer_destroy(s_textlayer_1);
}

static void wakeup_handler(/* WakeupId id, int32_t reason */) {
  show_wakeup();
}

void display_info(void) {
  if (!s_slide_running) {
    if (s_history_offset >= get_chart_history_count()) s_history_offset = 0;
    set_chart_selected_history_offset(s_history_offset);
    format_history_weight(s_history_offset, s_new_weight_text, sizeof(s_new_weight_text));
    if (s_weight_animation_layer) layer_mark_dirty(s_weight_animation_layer);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (tick_time->tm_yday != s_today) {
    move_back();
    layer_mark_dirty(s_chart);
    display_info();
  }
}

// Same hold-sensitive 5 -> 7 -> 5 pixel action as Swiss Chronograph.
// Navigation happens after the release animation, not on press.
static void button_anim_tick(void *context) {
  s_button_anim_timer = NULL;
  if (s_active_button < 0 || !s_button_marker) return;
  int i = s_active_button;
  if (s_button_held) {
    if (s_button_width[i] < 7) s_button_width[i]++;
    layer_mark_dirty(s_button_marker);
    if (s_button_width[i] < 7)
      s_button_anim_timer = app_timer_register(55, button_anim_tick, NULL);
  } else if (s_button_width[i] > 5) {
    s_button_width[i]--;
    layer_mark_dirty(s_button_marker);
    s_button_anim_timer = app_timer_register(55, button_anim_tick, NULL);
  } else {
    s_active_button = -1;
    if (i == 1) {
      int history_index = get_chart_history_index(s_history_offset);
      if (history_index >= 0) show_input_edit(history_index);
    }
    else if (i == 2) show_input();
  }
}

static void button_raw_down(ClickRecognizerRef recognizer, void *context) {
  ButtonId id = click_recognizer_get_button_id(recognizer);
  // UP navigates on every press, even when its visual pulse is settling.
  if (id == BUTTON_ID_UP) {
    change_history(-1);
    if (s_active_button >= 0) return;
  } else if (s_active_button >= 0) return;
  int i = id == BUTTON_ID_UP ? 0 : id == BUTTON_ID_SELECT ? 1 : 2;
  if (s_button_enter_x[i] != 0 || s_button_entering[i]) return;
  s_active_button = i;
  s_button_held = true;
  if (s_button_anim_timer) app_timer_cancel(s_button_anim_timer);
  s_button_anim_timer = app_timer_register(55, button_anim_tick, NULL);
}

static void button_raw_up(ClickRecognizerRef recognizer, void *context) {
  if (s_active_button < 0) return;
  s_button_held = false;
  if (s_button_anim_timer) app_timer_cancel(s_button_anim_timer);
  s_button_anim_timer = app_timer_register(55, button_anim_tick, NULL);
}

static bool s_back_press_rising = false;
static void back_press_tick(void *context) {
  s_back_press_timer = NULL;
  // Mirror the right buttons' exact 55 ms, one-pixel 5 -> 7 -> 5 pulse.
  if (s_back_press_rising) {
    if (s_back_width < 7) ++s_back_width;
    if (s_back_width >= 7) s_back_press_rising = false;
  } else if (s_back_width > 5) {
    --s_back_width;
  }
  if (s_back_marker) layer_mark_dirty(s_back_marker);
  if (s_back_press_rising || s_back_width > 5)
    s_back_press_timer = app_timer_register(55, back_press_tick, NULL);
}

static void back_left_click(ClickRecognizerRef recognizer, void *context) {
  // BACK has no usable release callback; replay the same right-button pulse.
  if (s_back_press_timer) {
    app_timer_cancel(s_back_press_timer);
    s_back_press_timer = NULL;
  }
  s_back_width = 5;
  s_back_press_rising = true;
  s_back_press_timer = app_timer_register(55, back_press_tick, NULL);
  change_history(1);
}

static void click_config_provider(void *context) {
  window_raw_click_subscribe(BUTTON_ID_UP, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, button_raw_down, button_raw_up, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_left_click);

}

#ifndef PBL_PLATFORM_APLITE
static double anim_percentage(AnimationProgress dist_normalized, double max) {
  return (((double)dist_normalized / (double)ANIMATION_NORMALIZED_MAX) * max);
}

static void height_update(Animation *anim, AnimationProgress dist_normalized) {
  set_multiplier(anim_percentage(dist_normalized, 1.0));

  layer_mark_dirty(s_chart);
}
#endif

static void handle_window_unload(Window* window) {
  s_main_visible = false;
  if (s_pulse_timer) {
    app_timer_cancel(s_pulse_timer);
    s_pulse_timer = NULL;
  }
  destroy_ui();
}

static void handle_window_appear(Window* window) {
  s_main_visible = true;
  if (!s_pulse_timer) s_pulse_timer = app_timer_register(PULSE_FRAME_MS, pulse_timer_callback, NULL);
  display_info();
  if (s_chart) layer_mark_dirty(s_chart);
  if (!s_buttons_introduced && !s_button_intro_timer)
    s_button_intro_timer = app_timer_register(500, button_intro_tick, NULL);
}

static void show_main(void) {
  for (int i = 0; i < 3; ++i) {
    s_button_width[i] = 5;
    s_button_enter_x[i] = 30;
    s_button_enter_frame[i] = 0;
    s_button_entering[i] = false;
  }
  s_buttons_introduced = false;
  s_back_expanded = false;
  s_back_width = 5;
  s_active_button = -1;
  s_button_held = false;
  s_history_offset = 0;
  set_chart_selected_history_offset(0);
  s_slide_running = false;
  s_slide_frame = 0;
  s_pulse_step = 0;
  set_chart_pulse_step(0);
  initialise_ui();

  layer_set_update_proc(s_chart, draw_chart_callback);

  display_info();

  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
    .appear = handle_window_appear
  });

  tick_timer_service_subscribe(HOUR_UNIT, tick_handler);

  window_stack_push(s_window, true);
}

static void hide_main(void) {
  tick_timer_service_unsubscribe();
  window_stack_remove(s_window, true);
}

static void config_received(DictionaryIterator *iter, void *context) {
  // Trace what the phone actually sends. This also catches missing label keys.
  Tuple *tuple = dict_read_first(iter);
  while (tuple) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Settings incoming key=%lu type=%d",
            (unsigned long)tuple->key, (int)tuple->type);
    tuple = dict_read_next(iter);
  }
  if (!chart_prefs_apply(iter)) return;
  s_history_offset = 0;
  s_incoming_offset = 0;
  s_slide_running = false;
  set_chart_selected_history_offset(0);
  display_info();
  if (s_chart) layer_mark_dirty(s_chart);
  if (s_weight_animation_layer) layer_mark_dirty(s_weight_animation_layer);
}

void weighttracker_sync_refresh(void) {
  s_history_offset = 0;
  s_incoming_offset = 0;
  s_slide_running = false;
  set_chart_selected_history_offset(0);
  if (s_chart) layer_mark_dirty(s_chart);
  display_info();
}

static void app_inbox_received(DictionaryIterator *iter, void *context) {
  if (dict_find(iter, MESSAGE_KEY_SYNC_COMMAND)) {
    sync_on_inbox(iter);
  } else {
    config_received(iter, context);
  }
}

void init() {
  pin_initialize();
  s_today = get_year_day();

#if __TESTING == 1
  test_data();
#endif

  load_data();
  chart_prefs_load();
  app_message_register_inbox_received(app_inbox_received);
  sync_init();
  app_message_open(256, 256);
  wakeup_service_subscribe(wakeup_handler);
}

static void deinit() {
  app_message_deregister_callbacks();
  save_data();
  pin_deinitalize();
}

#if __TESTING == 1
char *translate_launch_reason(AppLaunchReason reason) {
  switch (reason) {
    case APP_LAUNCH_SYSTEM: return "APP_LAUNCH_SYSTEM";
    case APP_LAUNCH_USER: return "APP_LAUNCH_USER";
    case APP_LAUNCH_WAKEUP: return "APP_LAUNCH_WAKEUP";
    case APP_LAUNCH_PHONE: return "APP_LAUNCH_PHONE";
    case APP_LAUNCH_WORKER: return "APP_LAUNCH_WORKER";
    case APP_LAUNCH_QUICK_LAUNCH: return "APP_LAUNCH_QUICK_LAUNCH";
    case APP_LAUNCH_TIMELINE_ACTION: return "APP_LAUNCH_TIMELINE_ACTION";
    default: return "UNKNOWN REASON";
  }
}
#endif

int main(void) {
#ifndef PBL_PLATFORM_APLITE
  bool do_anim = true;
#endif

  init();

  show_main();

#if __TESTING == 1
  LOG_VERSION();
#endif

  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    show_wakeup();
#ifndef PBL_PLATFORM_APLITE
    do_anim = false;
#endif
  }

#ifndef PBL_PLATFORM_APLITE
  if (do_anim) {
    set_multiplier(0.0);

    // Prepare animations
    AnimationImplementation height_impl = {
      .update = height_update
    };
    animate(ANIMATION_DURATION, ANIMATION_DELAY, &height_impl, false);
  } else {
    set_multiplier(1.0);
  }
#else
  set_multiplier(1.0);
#endif

  app_event_loop();
  hide_main();
  deinit();
}
