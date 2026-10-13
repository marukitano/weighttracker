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
static Layer *s_inputlayer;
static ActionBarLayer *s_actionbarlayer;

static char* selection_handle_get_text(int index, void *context) {
  double_str(s_weight, s_weigth_str, sizeof(s_weigth_str));
  return s_weigth_str;
}

static void selection_handle_complete(void *context) {
  if (s_edit_offset >= 0)
    update_history_weight(s_edit_offset, from_current_unit(s_weight));
  else
    set_current(from_current_unit(s_weight));
  hide_input();
}

static void selection_handle_dec(int index, uint8_t clicks, void *context) {
  if (s_weight > 0.0) {
    s_weight -= get_accuracy_step();
  }
}

static void selection_handle_inc(int index, uint8_t clicks, void *context) {
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
  action_bar_layer_set_background_color(s_actionbarlayer, GColorBlack);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_UP, s_res_up_arrow);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_SELECT, s_res_ok_button);
  action_bar_layer_set_icon(s_actionbarlayer, BUTTON_ID_DOWN, s_res_down_arrow);
  //layer_add_child(window_get_root_layer(s_window), (Layer *)s_actionbarlayer);

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
  window_destroy(s_window);
  text_layer_destroy(s_titlelayer);
  selection_layer_destroy(s_inputlayer);
  action_bar_layer_destroy(s_actionbarlayer);
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
  s_weight = to_current_unit(get_history_weight(newest_offset));
  s_weight = (double)((int)(s_weight * get_accuracy_multiplier() + 0.5)) / get_accuracy_multiplier();
  snprintf(s_title, sizeof(s_title), "Weight (%s)", get_short_unit_name());
  text_layer_set_text(s_titlelayer, s_title);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
  });
  window_stack_push(s_window, true);
}

void hide_input(void) {
  window_stack_remove(s_window, true);
}
