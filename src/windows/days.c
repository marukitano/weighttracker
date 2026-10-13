#include <pebble.h>
#include "../support/config.h"
#include "../support/reminder.h"
#include "../support/utils.h"
#include "days.h"
#include "time.h"

static Window *s_window;
//static TextLayer *s_textlayer_1;
static MenuLayer *s_menulayer_1;

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return NUM_OF_OPTIONS;
}

static int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  return PBL_IF_ROUND_ELSE(
    menu_layer_is_index_selected(menu_layer, cell_index) ?
      MENU_CELL_ROUND_FOCUSED_TALL_CELL_HEIGHT : MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT,
    DAYS_CELL_HEIGHT);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell_layer, "Every day", NULL, NULL);
      break;

    case 1:
      menu_cell_basic_draw(ctx, cell_layer, "Weekdays", NULL, NULL);
      break;

    case 2:
      menu_cell_basic_draw(ctx, cell_layer, "Weekends", NULL, NULL);
      break;
  }
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      (get_reminder_ptr())->days = EVERYDAY;
      break;

    case 1:
      (get_reminder_ptr())->days = WEEKDAYS;
      break;

    case 2:
      (get_reminder_ptr())->days = WEEKENDS;
      break;
  }

  set_has_reminders(1);
  hide_days();
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  /*
  // s_textlayer_1
  s_textlayer_1 = text_layer_create(GRect(0, 0, bounds.size.w, 28));
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, FOREGROUND);
  text_layer_set_text(s_textlayer_1, "Day");
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_textlayer_1);
  */

  // s_menulayer_1
  s_menulayer_1 = menu_layer_create(bounds);
  //s_menulayer_1 = menu_layer_create(GRect(0, 34, bounds.size.w, DAYS_CELL_HEIGHT * NUM_OF_OPTIONS));
  menu_layer_set_click_config_onto_window(s_menulayer_1, s_window);
  menu_layer_set_callbacks(s_menulayer_1, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_get_num_rows_callback,
    .get_cell_height = menu_get_cell_height_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback
  });
  menu_layer_set_normal_colors(s_menulayer_1, BACKGROUND, FOREGROUND);
  menu_layer_set_highlight_colors(s_menulayer_1, HIGHLIGHT, HLFOREGROUND);
  menu_layer_pad_bottom_enable(s_menulayer_1, false);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_menulayer_1);
/*
#ifdef PBL_ROUND
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_1, 4);
#endif
*/
}

static void destroy_ui(void) {
  window_destroy(s_window);
  menu_layer_destroy(s_menulayer_1);
  //text_layer_destroy(s_textlayer_1);
}

static void handle_window_unload(Window* window) {
  destroy_ui();
}

void show_days(void) {
  initialise_ui();

  menu_layer_set_selected_index(s_menulayer_1, MenuIndex(0, (int)(get_reminder_ptr())->days), MenuRowAlignNone, false);

  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
  });

  window_stack_push(s_window, true);
}

void hide_days(void) {
  hide_time(false);
  window_stack_remove(s_window, true);
}
