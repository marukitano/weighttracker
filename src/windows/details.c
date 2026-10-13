#include <pebble.h>
#include "../support/config.h"
#include "../support/data.h"
#include "../support/utils.h"
#include "../support/parameters.h"
#include "../windows/details.h"
#include "../windows/input.h"

#define RAIL_WIDTH 36
#define ROW_HEIGHT 30
#define ROW_TEXT_Y 5
#define MAX_LIST_HEIGHT (HISTORY_CAPACITY * ROW_HEIGHT)

static Window *s_window;
static ScrollLayer *s_scrolllayer;
static Layer *s_list_layer;
static Layer *s_button_marker;
static int s_selected = 0;

static void list_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int count = get_history_count();
  if (!count) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "No measurements", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(4, 5, bounds.size.w - 8, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  for (int i = 0; i < count; ++i) {
    int y = i * ROW_HEIGHT;
    bool active = i == s_selected;
    if (active) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, ROW_HEIGHT),
                         0, GCornerNone);
    }
    graphics_context_set_text_color(ctx, active ? GColorBlack : GColorWhite);
    uint32_t date = get_history_date(i);
    char date_str_buf[16];
    char weight_str[16];
    char value_str[24];
    snprintf(date_str_buf, sizeof(date_str_buf), "%02lu.%02lu.%04lu",
             (unsigned long)(date % 100),
             (unsigned long)((date / 100) % 100),
             (unsigned long)(date / 10000));
    double_str(to_current_unit(get_history_weight(i)), weight_str, sizeof(weight_str));
    snprintf(value_str, sizeof(value_str), "%s %s", weight_str,
             get_current_unit_index() == 0 ? "kg" : get_short_unit_name());
    // Fixed columns keep a clearly visible gap between date and weight.
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    graphics_draw_text(ctx, date_str_buf, font, GRect(4, y + ROW_TEXT_Y, 91, 23),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, value_str, font,
                       GRect(101, y + ROW_TEXT_Y, bounds.size.w - 103, 23),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void button_marker_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int centers[3] = {36, bounds.size.h / 2, bounds.size.h - 36};
  const int right = bounds.size.w;
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, 2, bounds.size.h), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 3; ++i) {
    graphics_fill_rect(ctx, GRect(right - 5, centers[i] - 26, 5, 52),
                       5, GCornersLeft);
    const int mid_x = right - 21;
    if (i == 1) {
      graphics_draw_line(ctx, GPoint(mid_x - 6, centers[i]),
                         GPoint(mid_x + 6, centers[i]));
      graphics_draw_line(ctx, GPoint(mid_x, centers[i] - 6),
                         GPoint(mid_x, centers[i] + 6));
    } else {
      const int tip_y = centers[i] + (i == 0 ? -4 : 4);
      const int arm_y = centers[i] + (i == 0 ? 3 : -3);
      graphics_draw_line(ctx, GPoint(mid_x - 6, arm_y), GPoint(mid_x, tip_y));
      graphics_draw_line(ctx, GPoint(mid_x, tip_y), GPoint(mid_x + 6, arm_y));
    }
  }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  show_input();
}

static void details_move(ClickRecognizerRef recognizer, void *context) {
  int count = get_history_count();
  if (count < 1) return;
  int step = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP ? -1 : 1;
  int target = s_selected + step;
  if (target < 0 || target >= count) return;
  s_selected = target;
  layer_mark_dirty(s_list_layer);

  GPoint offset = scroll_layer_get_content_offset(s_scrolllayer);
  GRect frame = layer_get_frame(scroll_layer_get_layer(s_scrolllayer));
  int visible_top = -offset.y;
  int visible_bottom = visible_top + frame.size.h;
  int row_top = s_selected * ROW_HEIGHT;
  int row_bottom = row_top + ROW_HEIGHT;
  if (row_top < visible_top) {
    scroll_layer_set_content_offset(s_scrolllayer, GPoint(0, -row_top), true);
  } else if (row_bottom > visible_bottom) {
    scroll_layer_set_content_offset(s_scrolllayer,
                                    GPoint(0, -(row_bottom - frame.size.h)), true);
  }
}

static void details_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, details_move);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, details_move);
}

static void refresh_details(void) {
  if (!s_scrolllayer || !s_list_layer) return;
  int count = get_history_count();
  if (count < 1) s_selected = 0;
  else if (s_selected >= count) s_selected = count - 1;
  int height = (count > 0 ? count : 1) * ROW_HEIGHT;
  if (height > MAX_LIST_HEIGHT) height = MAX_LIST_HEIGHT;
  GRect frame = layer_get_frame(s_list_layer);
  layer_set_frame(s_list_layer, GRect(0, 0, frame.size.w, height));
  scroll_layer_set_content_size(s_scrolllayer, GSize(frame.size.w, height));
  scroll_layer_set_content_offset(s_scrolllayer, GPoint(0, 0), false);
  s_selected = 0; // Always start on the newest measurement.
  layer_mark_dirty(s_list_layer);
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  int list_width = bounds.size.w - RAIL_WIDTH;
  s_scrolllayer = scroll_layer_create(GRect(0, 0, list_width, bounds.size.h));
  s_list_layer = layer_create(GRect(0, 0, list_width, ROW_HEIGHT));
  layer_set_update_proc(s_list_layer, list_draw);
  scroll_layer_add_child(s_scrolllayer, s_list_layer);
  layer_add_child(root, scroll_layer_get_layer(s_scrolllayer));
  s_button_marker = layer_create(GRect(list_width, 0, RAIL_WIDTH, bounds.size.h));
  layer_set_update_proc(s_button_marker, button_marker_update);
  layer_add_child(root, s_button_marker);
  refresh_details();
}

static void handle_window_appear(Window *window) {
  refresh_details(); // Picks up a newly entered weight on return.
}

static void handle_window_unload(Window *window) {
  window_destroy(s_window);
  layer_destroy(s_list_layer);
  scroll_layer_destroy(s_scrolllayer);
  layer_destroy(s_button_marker);
  s_window = NULL;
  s_scrolllayer = NULL;
  s_list_layer = NULL;
  s_button_marker = NULL;
}

void show_details(void) {
  initialise_ui();
  window_set_click_config_provider(s_window, details_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
    .appear = handle_window_appear
  });
  window_stack_push(s_window, true);
}

void hide_details(void) {
  window_stack_remove(s_window, true);
}
