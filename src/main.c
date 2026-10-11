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
static TextLayer *s_textlayer_2;
static Layer *s_chart;
static Layer *s_button_marker;
static AppTimer *s_button_anim_timer;
static uint8_t s_button_width = 5;
static uint8_t s_button_anim_phase = 0;
static AppTimer *s_pulse_timer;
static uint8_t s_pulse_step;
static bool s_main_visible;

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
  graphics_context_set_fill_color(ctx, FOREGROUND);
  graphics_context_set_stroke_color(ctx, FOREGROUND);
  graphics_context_set_stroke_width(ctx, 2);

  for (int i = 0; i < 3; ++i) {
    int marker_width = (i == 2) ? s_button_width : 5;
    graphics_fill_rect(ctx,
                       GRect(edge_x - marker_width, center_y[i] - 26,
                             marker_width, 52),
                       5, GCornersLeft);
    const int icon_right = edge_x - 14;
    const int icon_left = icon_right - 13;
    if (i == 1) {
      for (int offset = -6; offset <= 6; offset += 6) {
        graphics_draw_line(ctx, GPoint(icon_left, center_y[i] + offset),
                           GPoint(icon_right, center_y[i] + offset));
      }
    } else {
      const int tip_x = (i == 0) ? icon_left + 2 : icon_right - 2;
      const int outer_x = (i == 0) ? icon_right - 2 : icon_left + 2;
      graphics_draw_line(ctx, GPoint(outer_x, center_y[i] - 7),
                         GPoint(tip_x, center_y[i]));
      graphics_draw_line(ctx, GPoint(tip_x, center_y[i]),
                         GPoint(outer_x, center_y[i] + 7));
    }
  }
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  // s_textlayer_2
  s_textlayer_2 = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(14, 0), bounds.size.w, PBL_IF_ROUND_ELSE(24, 28)));
  text_layer_set_background_color(s_textlayer_2, GColorClear);
  text_layer_set_text_color(s_textlayer_2, FOREGROUND);
  text_layer_set_text(s_textlayer_2, "");
  text_layer_set_text_alignment(s_textlayer_2, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_2, fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_24_BOLD)));
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_textlayer_2);

  // The Time 2 (emery) has a 200x228 display. Give its chart the full
  // available width and height while retaining the original layout elsewhere.
  // draw_chart_callback derives its geometry from these layer bounds.
  // s_chart
#ifdef PBL_PLATFORM_EMERY
  s_chart = layer_create(GRect(0, 34, bounds.size.w, bounds.size.h - 34));
#else
  s_chart = layer_create(GRect(PBL_IF_ROUND_ELSE((bounds.size.w - 144) / 2 - 4, 0), 40, 144, 110));
#endif
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_chart);

  // s_textlayer_1
#ifdef PBL_PLATFORM_EMERY
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - 58, bounds.size.w, 44));
#else
  s_textlayer_1 = text_layer_create(GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(38, 20), bounds.size.w, PBL_IF_ROUND_ELSE(38, 24)));
#endif
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, FOREGROUND);
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_textlayer_1);

  // Lower right button indicator: center 36px above bottom, as in Swiss Chronograph.
  s_button_marker = layer_create(GRect(bounds.size.w - 36, 0, 36, bounds.size.h));
  layer_set_update_proc(s_button_marker, button_marker_update);
  layer_add_child(window_get_root_layer(s_window), s_button_marker);

#ifdef PBL_ROUND
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_2, 2);
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_1, 2);
#endif
}

static void destroy_ui(void) {
  window_destroy(s_window);
  text_layer_destroy(s_textlayer_2);
  layer_destroy(s_chart);
  if (s_button_anim_timer) {
    app_timer_cancel(s_button_anim_timer);
    s_button_anim_timer = NULL;
  }
  layer_destroy(s_button_marker);
  text_layer_destroy(s_textlayer_1);
}

static void wakeup_handler(/* WakeupId id, int32_t reason */) {
  show_wakeup();
}

void display_info(void) {
  text_layer_set_text(s_textlayer_2,
                      get_chart_days() == 30 ? "Last 30 days" : "Last 15 days");
  // Temporary fixed preview caption while demo chart is enabled.
  text_layer_set_text(s_textlayer_1, "112kg");
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (tick_time->tm_yday != s_today) {
    move_back();
    layer_mark_dirty(s_chart);
    display_info();
  }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  show_settings();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  show_input();
}

// Swiss Chronograph-inspired 5px -> 7px -> 5px press feedback.
static void lower_button_animation(void *context) {
  s_button_anim_timer = NULL;
  if (s_button_anim_phase == 0) {
    s_button_width = 7;
    s_button_anim_phase = 1;
    layer_mark_dirty(s_button_marker);
    s_button_anim_timer = app_timer_register(55, lower_button_animation, NULL);
  } else {
    s_button_width = 5;
    s_button_anim_phase = 0;
    layer_mark_dirty(s_button_marker);
    show_details();
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_button_anim_timer) return;
  s_button_anim_phase = 0;
  lower_button_animation(NULL);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
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
  s_button_width = 5;
  s_button_anim_phase = 0;
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
