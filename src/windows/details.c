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
#define MAX_LIST_HEIGHT ((HISTORY_CAPACITY + 1) * ROW_HEIGHT)

static Window *s_window;
static ScrollLayer *s_scrolllayer;
static Layer *s_list_layer;
static Layer *s_button_marker;
static Layer *s_back_marker;
static int s_selected = 0;
static AppTimer *s_entry_timer;
static AppTimer *s_press_timer;
static uint8_t s_entry_step;
static int8_t s_entry_shift = 30;
static int8_t s_pressed_button = -1;
static bool s_pressed_down;
static uint8_t s_press_width[3] = {5, 5, 5};
// Swiss Chronograph src/c/touch.c: 42 ms, original overshoot sequence.
static const int8_t s_bounce_offsets[] = {30, 18, 8, -3, 2, -1, 0};

static void entry_tick(void *context) {
  s_entry_timer = NULL;
  if (s_entry_step < sizeof(s_bounce_offsets) - 1) ++s_entry_step;
  s_entry_shift = s_bounce_offsets[s_entry_step];
  if (s_button_marker) layer_mark_dirty(s_button_marker);
  if (s_back_marker) layer_mark_dirty(s_back_marker);
  if (s_entry_step < sizeof(s_bounce_offsets) - 1)
    s_entry_timer = app_timer_register(42, entry_tick, NULL);
}

static void entry_start(void *context) {
  s_entry_timer = NULL;
  s_entry_step = 0;
  s_entry_shift = 30;
  s_entry_timer = app_timer_register(42, entry_tick, NULL);
}

static void details_move(ClickRecognizerRef recognizer, void *context);
static void select_click_handler(ClickRecognizerRef recognizer, void *context);
static void press_tick(void *context) {
  s_press_timer = NULL;
  if (s_pressed_button < 0) return;
  int i = s_pressed_button;
  if (s_pressed_down) {
    if (s_press_width[i] < 7) ++s_press_width[i];
  } else if (s_press_width[i] > 5) {
    --s_press_width[i];
  } else {
    s_pressed_button = -1;
    if (i == 1) select_click_handler(NULL, NULL);
    else details_move(NULL, (void *)(intptr_t)(i == 0 ? -1 : 1));
    return;
  }
  layer_mark_dirty(s_button_marker);
  s_press_timer = app_timer_register(55, press_tick, NULL);
}

static void raw_down(ClickRecognizerRef recognizer, void *context) {
  if (s_pressed_button >= 0 || s_entry_shift != 0) return;
  ButtonId id = click_recognizer_get_button_id(recognizer);
  s_pressed_button = id == BUTTON_ID_UP ? 0 : id == BUTTON_ID_SELECT ? 1 : 2;
  s_pressed_down = true;
  s_press_timer = app_timer_register(55, press_tick, NULL);
}
static void raw_up(ClickRecognizerRef recognizer, void *context) {
  if (s_pressed_button < 0) return;
  s_pressed_down = false;
  if (s_press_timer) app_timer_cancel(s_press_timer);
  s_press_timer = app_timer_register(55, press_tick, NULL);
}


static void list_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int count = get_history_count();
  for (int i = 0; i <= count; ++i) {
    int y = i * ROW_HEIGHT;
    bool active = i == s_selected;
    if (active) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, ROW_HEIGHT),
                         0, GCornerNone);
    }
    graphics_context_set_text_color(ctx, active ? GColorBlack : GColorWhite);
    if (i == 0) {
      graphics_draw_text(ctx, "+", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                         GRect(0, y - 3, bounds.size.w, ROW_HEIGHT + 6),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      continue;
    }
    int history_offset = i - 1;
    uint32_t date = get_history_date(history_offset);
    char date_str_buf[16];
    char weight_str[16];
    char value_str[24];
    snprintf(date_str_buf, sizeof(date_str_buf), "%02lu.%02lu.%04lu",
             (unsigned long)(date % 100),
             (unsigned long)((date / 100) % 100),
             (unsigned long)(date / 10000));
    double_str(to_current_unit(get_history_weight(history_offset)), weight_str, sizeof(weight_str));
    snprintf(value_str, sizeof(value_str), "%s %s", weight_str,
             get_current_unit_index() == 0 ? "kg" : get_short_unit_name());
    // Weight first, date second, with room for the right-side icons.
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    graphics_draw_text(ctx, value_str, font, GRect(4, y + ROW_TEXT_Y, 76, 23),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, date_str_buf, font,
                       GRect(85, y + ROW_TEXT_Y, bounds.size.w - 121, 23),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void button_marker_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int centers[3] = {36, bounds.size.h / 2, bounds.size.h - 36};
  const int right = bounds.size.w;
  // Transparent side overlay: only the black tabs and icons remain.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  if (s_entry_shift >= 30) return;
  for (int i = 0; i < 3; ++i) {
    int width = s_press_width[i] + (s_entry_shift > 0 ? s_entry_shift : 0);
    graphics_fill_rect(ctx, GRect(right - width,
                                 centers[i] - 26, width, 52),
                       5, GCornersLeft);
    const int mid_x = right - 21 + s_entry_shift;
    if (i == 1) {
      // Diagonal edit pencil, pointing toward the lower left.
      graphics_draw_line(ctx, GPoint(mid_x - 6, centers[i] + 6),
                         GPoint(mid_x + 5, centers[i] - 5));
      graphics_draw_line(ctx, GPoint(mid_x - 4, centers[i] + 8),
                         GPoint(mid_x + 7, centers[i] - 3));
      graphics_draw_line(ctx, GPoint(mid_x + 5, centers[i] - 5),
                         GPoint(mid_x + 7, centers[i] - 3));
      graphics_draw_line(ctx, GPoint(mid_x - 6, centers[i] + 6),
                         GPoint(mid_x - 4, centers[i] + 8));
    } else {
      const int tip_y = centers[i] + (i == 0 ? -4 : 4);
      const int arm_y = centers[i] + (i == 0 ? 3 : -3);
      graphics_draw_line(ctx, GPoint(mid_x - 6, arm_y), GPoint(mid_x, tip_y));
      graphics_draw_line(ctx, GPoint(mid_x, tip_y), GPoint(mid_x + 6, arm_y));
    }
  }
}

// Mirrored Swiss Chronograph back button, 36px from top, 5x52px.
static void back_marker_update(Layer *layer, GContext *ctx) {
  if (s_entry_shift >= 30) return;
  int cx = 15 - s_entry_shift, cy = 36;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 10, 5 + (s_entry_shift > 0 ? s_entry_shift : 0), 52),
                     5, GCornersRight);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  // Return arrow points left (not an X).
  graphics_draw_line(ctx, GPoint(cx + 6, cy), GPoint(cx - 5, cy));
  graphics_draw_line(ctx, GPoint(cx - 5, cy), GPoint(cx + 1, cy - 6));
  graphics_draw_line(ctx, GPoint(cx - 5, cy), GPoint(cx + 1, cy + 6));
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_selected == 0) show_input();
  else show_input_edit(s_selected - 1);
}

static void details_move(ClickRecognizerRef recognizer, void *context) {
  int count = get_history_count() + 1;
  if (count < 1) return;
  int step = (int)(intptr_t)context;
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
  window_raw_click_subscribe(BUTTON_ID_UP, raw_down, raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, raw_down, raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, raw_down, raw_up, NULL);
}

static void refresh_details(void) {
  if (!s_scrolllayer || !s_list_layer) return;
  int count = get_history_count();
  if (s_selected > count) s_selected = count;
  int height = (count + 1) * ROW_HEIGHT;
  if (height > MAX_LIST_HEIGHT) height = MAX_LIST_HEIGHT;
  GRect frame = layer_get_frame(s_list_layer);
  layer_set_frame(s_list_layer, GRect(0, 0, frame.size.w, height));
  scroll_layer_set_content_size(s_scrolllayer, GSize(frame.size.w, height));
  GRect view = layer_get_frame(scroll_layer_get_layer(s_scrolllayer));
  int scroll_top = s_selected * ROW_HEIGHT - view.size.h + ROW_HEIGHT;
  if (scroll_top < 0) scroll_top = 0;
  scroll_layer_set_content_offset(s_scrolllayer, GPoint(0, -scroll_top), false);
  // Keep the selection visible when returning from the editor.
  layer_mark_dirty(s_list_layer);
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  int list_width = bounds.size.w;
  s_scrolllayer = scroll_layer_create(GRect(0, 0, list_width, bounds.size.h));
  s_list_layer = layer_create(GRect(0, 0, list_width, ROW_HEIGHT));
  layer_set_update_proc(s_list_layer, list_draw);
  scroll_layer_add_child(s_scrolllayer, s_list_layer);
  layer_add_child(root, scroll_layer_get_layer(s_scrolllayer));
  s_button_marker = layer_create(GRect(bounds.size.w - RAIL_WIDTH, 0, RAIL_WIDTH, bounds.size.h));
  layer_set_update_proc(s_button_marker, button_marker_update);
  layer_add_child(root, s_button_marker);
  s_back_marker = layer_create(GRect(0, 0, RAIL_WIDTH, bounds.size.h));
  layer_set_update_proc(s_back_marker, back_marker_update);
  layer_add_child(root, s_back_marker);
  refresh_details();
}

static void handle_window_appear(Window *window) {
  refresh_details(); // Picks up a newly entered weight on return.
  if (s_entry_shift >= 30 && !s_entry_timer)
    s_entry_timer = app_timer_register(500, entry_start, NULL);
}

static void handle_window_unload(Window *window) {
  if (s_entry_timer) { app_timer_cancel(s_entry_timer); s_entry_timer = NULL; }
  if (s_press_timer) { app_timer_cancel(s_press_timer); s_press_timer = NULL; }
  window_destroy(s_window);
  layer_destroy(s_list_layer);
  scroll_layer_destroy(s_scrolllayer);
  layer_destroy(s_button_marker);
  layer_destroy(s_back_marker);
  s_window = NULL;
  s_scrolllayer = NULL;
  s_list_layer = NULL;
  s_button_marker = NULL;
  s_back_marker = NULL;
}

void show_details(void) {
  s_entry_shift = 30;
  s_entry_step = 0;
  s_pressed_button = -1;
  s_pressed_down = false;
  for (int i = 0; i < 3; ++i) s_press_width[i] = 5;
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
