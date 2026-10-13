#include <pebble.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/data.h"
#include "../support/parameters.h"
#include "../components/selection_layer.h"
#include "../windows/input.h"

static double s_weight = 80.0;
static int s_edit_offset = -1;
static char s_title[15];
static char s_weigth_str[10];

static Window *s_window;
static GBitmap *s_res_up_arrow;
static GBitmap *s_res_ok_button;
static GBitmap *s_res_down_arrow;
static TextLayer *s_titlelayer;
static TextLayer *s_edit_date_layer;
static char s_edit_date_text[24];
static Layer *s_inputlayer;
static ActionBarLayer *s_actionbarlayer;
static AppTimer *s_controls_timer;
static AppTimer *s_controls_delay;
static AppTimer *s_controls_press_timer;
static AppTimer *s_save_timer;
static bool s_controls_pressed;
static uint8_t s_controls_step;
static int16_t s_controls_offset = 30;
static GRect s_controls_frame;
static const int8_t s_controls_bounce[] = {30, 18, 8, -3, 2, -1, 0};

static void controls_bounce_tick(void *context) {
  s_controls_timer = NULL;
  if (!s_actionbarlayer) return;
  if (s_controls_step < sizeof(s_controls_bounce) - 1) ++s_controls_step;
  s_controls_offset = s_controls_bounce[s_controls_step];
  layer_set_frame(action_bar_layer_get_layer(s_actionbarlayer),
                  GRect(s_controls_frame.origin.x + s_controls_offset - (s_controls_pressed ? 2 : 0),
                        s_controls_frame.origin.y,
                        s_controls_frame.size.w, s_controls_frame.size.h));
  if (s_controls_step < sizeof(s_controls_bounce) - 1)
    s_controls_timer = app_timer_register(42, controls_bounce_tick, NULL);
}
static void controls_start(void *context) {
  s_controls_delay = NULL;
  s_controls_step = 0;
  s_controls_timer = app_timer_register(42, controls_bounce_tick, NULL);
}
static void controls_appear(Window *window) {
  if (!s_controls_delay)
    s_controls_delay = app_timer_register(500, controls_start, NULL);
}


static void controls_set_press(bool pressed) {
  if (!s_actionbarlayer) return;
  s_controls_pressed = pressed;
  layer_set_frame(action_bar_layer_get_layer(s_actionbarlayer),
                  GRect(s_controls_frame.origin.x + s_controls_offset
                            - (pressed ? 2 : 0),
                        s_controls_frame.origin.y,
                        s_controls_frame.size.w, s_controls_frame.size.h));
}
static void controls_release(void *context) {
  s_controls_press_timer = NULL;
  controls_set_press(false);
}
static void controls_press(void) {
  if (s_controls_press_timer) {
    app_timer_cancel(s_controls_press_timer);
    s_controls_press_timer = NULL;
  }
  controls_set_press(true);
  s_controls_press_timer = app_timer_register(110, controls_release, NULL);
}
static void save_after_press(void *context) {
  s_save_timer = NULL;
  if (s_edit_offset >= 0)
    update_history_weight(s_edit_offset, from_current_unit(s_weight));
  else
    set_current(from_current_unit(s_weight));
  hide_input();
}

static char* selection_handle_get_text(int index, void *context) {
  double_str(s_weight, s_weigth_str, sizeof(s_weigth_str));
  return s_weigth_str;
}

static void selection_handle_complete(void *context) {
  controls_press();
  if (!s_save_timer)
    s_save_timer = app_timer_register(110, save_after_press, NULL);
}

static void selection_handle_dec(int index, uint8_t clicks, void *context) {
  controls_press();
  if (s_weight > 0.0) {
    s_weight -= get_accuracy_step();
  }
}

static void selection_handle_inc(int index, uint8_t clicks, void *context) {
  controls_press();
  s_weight += get_accuracy_step();
}

static void initialise_ui(void) {
  s_window = window_create();

  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(window_layer);
  s_res_up_arrow = gbitmap_create_with_resource(RESOURCE_ID_ACTION_BAR_UP);
  s_res_ok_button = gbitmap_create_with_resource(RESOURCE_ID_ACTION_BAR_CHECK);
  s_res_down_arrow = gbitmap_create_with_resource(RESOURCE_ID_ACTION_BAR_DOWN);

  s_actionbarlayer = action_bar_layer_create();
  action_bar_layer_add_to_window(s_actionbarlayer, s_window);
  s_controls_frame = layer_get_frame(action_bar_layer_get_layer(s_actionbarlayer));
  s_controls_offset = 30;
  s_controls_step = 0;
  layer_set_frame(action_bar_layer_get_layer(s_actionbarlayer),
                  GRect(s_controls_frame.origin.x + 30, s_controls_frame.origin.y,
                        s_controls_frame.size.w, s_controls_frame.size.h));
  action_bar_layer_set_background_color(s_actionbarlayer, GColorBlack);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_UP, s_res_up_arrow);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_SELECT, s_res_ok_button);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_DOWN, s_res_down_arrow);
  //layer_add_child(window_get_root_layer(s_window), (Layer *)s_actionbarlayer);

  // Existing measurement: show its fixed date above the weight editor.
  s_edit_date_layer = text_layer_create(GRect(0, (bounds.size.h - 34) / 2 - 59,
                                            bounds.size.w - ACTION_BAR_WIDTH, 27));
  text_layer_set_background_color(s_edit_date_layer, GColorClear);
  text_layer_set_text_color(s_edit_date_layer, GColorWhite);
  text_layer_set_text_alignment(s_edit_date_layer, GTextAlignmentCenter);
  text_layer_set_font(s_edit_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_edit_date_layer));
  layer_set_hidden(text_layer_get_layer(s_edit_date_layer), true);

  // s_titlelayer
  s_titlelayer = text_layer_create(GRect(0, (bounds.size.h - 34) / 2 - 28, bounds.size.w - ACTION_BAR_WIDTH, 24));
  text_layer_set_background_color(s_titlelayer, GColorClear);
  text_layer_set_text_color(s_titlelayer, FOREGROUND);
  text_layer_set_text(s_titlelayer, "Weight (kg)");
  text_layer_set_text_alignment(s_titlelayer, GTextAlignmentCenter);
  text_layer_set_font(s_titlelayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_titlelayer);

  s_inputlayer = selection_layer_create(GRect(10, (bounds.size.h - 34) / 2, bounds.size.w - ACTION_BAR_WIDTH - 20, 34), 1);
  selection_layer_set_cell_width(s_inputlayer, 0, bounds.size.w - ACTION_BAR_WIDTH - 20);
  selection_layer_set_cell_padding(s_inputlayer, 4);
  selection_layer_set_active_bg_color(s_inputlayer, HIGHLIGHT);
  selection_layer_set_inactive_bg_color(s_inputlayer, INACTIVECELL);
  selection_layer_set_active_fg_color(s_inputlayer, ACTIVETEXT);
  selection_layer_set_inactive_fg_color(s_inputlayer, INACTIVETEXT);
  selection_layer_set_click_config_onto_actionbar(s_inputlayer, s_actionbarlayer);
  selection_layer_set_callbacks(s_inputlayer, NULL, (SelectionLayerCallbacks) {
    .get_cell_text = selection_handle_get_text,
    .complete = selection_handle_complete,
    .increment = selection_handle_inc,
    .decrement = selection_handle_dec,
  });
  layer_add_child(window_get_root_layer(s_window), s_inputlayer);
}

static void destroy_ui(void) {
  if (s_controls_delay) { app_timer_cancel(s_controls_delay); s_controls_delay = NULL; }
  if (s_controls_timer) { app_timer_cancel(s_controls_timer); s_controls_timer = NULL; }
  if (s_controls_press_timer) { app_timer_cancel(s_controls_press_timer); s_controls_press_timer = NULL; }
  if (s_save_timer) { app_timer_cancel(s_save_timer); s_save_timer = NULL; }
  window_destroy(s_window);
  text_layer_destroy(s_titlelayer);
  text_layer_destroy(s_edit_date_layer);
  selection_layer_destroy(s_inputlayer);
  action_bar_layer_destroy(s_actionbarlayer);
  s_actionbarlayer = NULL;
  gbitmap_destroy(s_res_up_arrow);
  gbitmap_destroy(s_res_ok_button);
  gbitmap_destroy(s_res_down_arrow);
}

static void handle_window_unload(Window* window) {
  destroy_ui();
}

void show_input(void) {
  s_edit_offset = -1;
  initialise_ui();

  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
    .appear = controls_appear,
  });

  s_weight = to_current_unit(get_latest_weight());
  s_weight = (double)((int)(s_weight * get_accuracy_multiplier() + 0.5)) / get_accuracy_multiplier();

  snprintf(s_title, sizeof(s_title), "Weight (%s)", get_short_unit_name());
  text_layer_set_text(s_titlelayer, s_title);

  window_stack_push(s_window, true);
}

void show_input_edit(int newest_offset) {
  if (newest_offset < 0 || newest_offset >= get_history_count()) return;
  s_edit_offset = newest_offset;
  initialise_ui();
  uint32_t date = get_history_date(newest_offset);
  snprintf(s_edit_date_text, sizeof(s_edit_date_text), "%02lu.%02lu.%04lu",
           (unsigned long)(date % 100),
           (unsigned long)((date / 100) % 100),
           (unsigned long)(date / 10000));
  text_layer_set_text(s_edit_date_layer, s_edit_date_text);
  layer_set_hidden(text_layer_get_layer(s_edit_date_layer), false);
  s_weight = to_current_unit(get_history_weight(newest_offset));
  s_weight = (double)((int)(s_weight * get_accuracy_multiplier() + 0.5)) / get_accuracy_multiplier();
  snprintf(s_title, sizeof(s_title), "Weight (%s)", get_short_unit_name());
  text_layer_set_text(s_titlelayer, s_title);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
    .appear = controls_appear,
  });
  window_stack_push(s_window, true);
}

void hide_input(void) {
  window_stack_remove(s_window, true);
}
