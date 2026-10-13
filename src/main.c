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
#include "windows/settings.h"
#include "windows/details.h"
#include "windows/input.h"
#include "windows/welcome.h"
#include "windows/wakeup.h"
#include "main.h"

static int s_today;

static Window *s_window;
static TextLayer *s_textlayer_1;
static Layer *s_chart;
static Layer *s_button_marker;
static AppTimer *s_button_anim_timer;
static uint8_t s_button_width[3] = {5, 5, 5};
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
// Upper: back (<), middle: list, lower: forward (>).
static void button_marker_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int center_y[3] = {36, bounds.size.h / 2, bounds.size.h - 36};
  const int edge_x = bounds.size.w;
  // Continuous red side rail behind all three button markers and icons.
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  // 2px black divider between the chart and the red control rail.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, 2, bounds.size.h), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);

  for (int i = 0; i < 3; ++i) {
    int marker_width = s_button_width[i];
    graphics_fill_rect(ctx,
                       GRect(edge_x - marker_width, center_y[i] - 26,
                             marker_width, 52),
                       5, GCornersLeft);
    const int icon_right = edge_x - 14;
    const int icon_left = icon_right - 13;
    if (i == 1) {
      // Two-line list icon: longer upper stroke, shorter lower stroke.
      graphics_draw_line(ctx, GPoint(icon_left, center_y[i] - 4),
                         GPoint(icon_right, center_y[i] - 4));
      graphics_draw_line(ctx, GPoint(icon_left, center_y[i] + 4),
                         GPoint(icon_right - 5, center_y[i] + 4));
    } else {
      // Upper key: right chevron; lower key: left chevron.
      const int mid_x = (icon_left + icon_right) / 2;
      const int tip_x = mid_x + (i == 0 ? 4 : -4);
      const int arm_x = mid_x + (i == 0 ? -3 : 3);
      graphics_draw_line(ctx, GPoint(arm_x, center_y[i] - 6),
                         GPoint(tip_x, center_y[i]));
      graphics_draw_line(ctx, GPoint(tip_x, center_y[i]),
                         GPoint(arm_x, center_y[i] + 6));
    }
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
static void draw_outlined_weight(GContext *ctx, const char *label, GFont font,
                                 GRect rect) {
  graphics_context_set_text_color(ctx, CHARTCOLOR);
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
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, label, font, rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void weight_slide_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int height = bounds.size.h;
  GFont font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  if (!s_slide_running) {
    draw_outlined_weight(ctx, s_new_weight_text, font, bounds);
    return;
  }
  int travel = (height * s_slide_frame) / 8;
  draw_outlined_weight(ctx, s_old_weight_text, font,
                       GRect(0, -s_slide_direction * travel, bounds.size.w, height));
  draw_outlined_weight(ctx, s_new_weight_text, font,
                       GRect(0, s_slide_direction * (height - travel), bounds.size.w, height));
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

static void change_history(int direction) {
  if (s_slide_running) return;
  int target = s_history_offset + direction;
  if (target < 0 || target >= get_chart_history_count()) return;
  snprintf(s_old_weight_text, sizeof(s_old_weight_text), "%s", s_new_weight_text);
  s_incoming_offset = target;
  set_chart_selected_history_offset(target);
  if (s_chart) layer_mark_dirty(s_chart);
  s_slide_direction = direction;
  format_history_weight(target, s_new_weight_text, sizeof(s_new_weight_text));
  s_slide_running = true;
  s_slide_frame = 0;
  s_weight_animation_timer = app_timer_register(35, slide_tick, NULL);
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  // The Time 2 (emery) has a 200x228 display. Give its chart the full
  // available width and height while retaining the original layout elsewhere.
  // draw_chart_callback derives its geometry from these layer bounds.
  // s_chart
#ifdef PBL_PLATFORM_EMERY
  s_chart = layer_create(GRect(0, 0, bounds.size.w - 36, bounds.size.h));
#else
  s_chart = layer_create(GRect(PBL_IF_ROUND_ELSE((bounds.size.w - 144) / 2 - 4, 0), 40, 144, 110));
#endif
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_chart);

  // s_textlayer_1
#ifdef PBL_PLATFORM_EMERY
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - 72, bounds.size.w - 36, 72));
#else
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(38, 20), bounds.size.w, PBL_IF_ROUND_ELSE(38, 24)));
#endif
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, FOREGROUND);
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  // Clip the old/new animated weights to the caption area.
  s_weight_animation_layer = layer_create(layer_get_frame(text_layer_get_layer(s_textlayer_1)));
  layer_set_clips(s_weight_animation_layer, true);
  layer_set_update_proc(s_weight_animation_layer, weight_slide_draw);
  layer_add_child(window_get_root_layer(s_window), s_weight_animation_layer);

  // Lower right button indicator: center 36px above bottom, as in Swiss Chronograph.
  s_button_marker = layer_create(GRect(bounds.size.w - 36, 0, 36, bounds.size.h));
  layer_set_update_proc(s_button_marker, button_marker_update);
  layer_add_child(window_get_root_layer(s_window), s_button_marker);

#ifdef PBL_ROUND
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_1, 2);
#endif
}

static void destroy_ui(void) {
  window_destroy(s_window);
  layer_destroy(s_chart);
  if (s_button_anim_timer) {
    app_timer_cancel(s_button_anim_timer);
    s_button_anim_timer = NULL;
  }
  layer_destroy(s_button_marker);
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
    if (i == 0) change_history(-1);
    else if (i == 1) show_details();
    else change_history(1);
  }
}

static void button_raw_down(ClickRecognizerRef recognizer, void *context) {
  if (s_active_button >= 0) return;
  ButtonId id = click_recognizer_get_button_id(recognizer);
  int i = id == BUTTON_ID_UP ? 0 : id == BUTTON_ID_SELECT ? 1 : 2;
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

static void click_config_provider(void *context) {
  window_raw_click_subscribe(BUTTON_ID_UP, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, button_raw_down, button_raw_up, NULL);
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
}

static void show_main(void) {
  for (int i = 0; i < 3; ++i) s_button_width[i] = 5;
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

void init() {
  pin_initialize();
  s_today = get_year_day();

#if __TESTING == 1
  test_data();
#endif

  load_data();
  wakeup_service_subscribe(wakeup_handler);
}

static void deinit() {
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
  } else if (get_stored_version() != get_current_version()) {
    show_welcome();
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
