#include <pebble.h>
#include "../support/config.h"
#include "../support/reminder.h"
#include "../support/utils.h"
#include "../support/parameters.h"
#include "../support/data.h"
#include "../windows/settings.h"
#include "../windows/welcome.h"
#include "../windows/time.h"

static Window *s_window;
static MenuLayer *s_menulayer_1;

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return NUM_FIRST_MENU_ITEMS;
}

static int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  return PBL_IF_ROUND_ELSE(
    menu_layer_is_index_selected(menu_layer, cell_index) ?
      MENU_CELL_ROUND_FOCUSED_TALL_CELL_HEIGHT : MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT,
    CELL_HEIGHT);
}

static void draw_menu_item(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, const char* title, const char* subtitle) {
  menu_cell_basic_draw(ctx, cell_layer, title, PBL_IF_ROUND_ELSE(
      menu_layer_is_index_selected(s_menulayer_1, cell_index) ? subtitle : NULL, subtitle),
    NULL);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  static char buffer[50];

  switch (cell_index->row) {
    case 0:
      draw_menu_item(ctx, cell_layer, cell_index, "Unit type", get_long_unit_name());
      break;

    case 1:
      draw_menu_item(ctx, cell_layer, cell_index, "Date format", get_date_format_str());
      break;

    case 2:
      draw_menu_item(ctx, cell_layer, cell_index, "First day of week", (is_monday_first() ? "Monday" : "Sunday"));
      break;

    case 3:
      snprintf(buffer, sizeof(buffer), "%s", get_reminder_str());
      draw_menu_item(ctx, cell_layer, cell_index, "Reminder", buffer);
      break;

    case 4:
      draw_menu_item(ctx, cell_layer, cell_index, "Chart mode",
                     get_chart_mode() ? "Calendar days" : "Measurements");
      break;

    case 5:
      snprintf(buffer, sizeof(buffer), "%d %s", get_chart_range(),
               get_chart_mode() ? "days" : "measurements");
      draw_menu_item(ctx, cell_layer, cell_index, "Chart range", buffer);
      break;

    case 6:
      draw_menu_item(ctx, cell_layer, cell_index, "About page", NULL);
      break;
  }
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      next_unit_index();
      break;

    case 1:
      next_format();
      break;

    case 2:
      toggle_first_day();
      break;

    case 3:
      if (!get_has_reminders()) {
        show_time();
      } else {
        set_has_reminders(0);
        cancell_all_reminders();
        menu_layer_reload_data(menu_layer);
      }

      break;

    case 4:
        toggle_chart_mode();
        break;

    case 5:
        next_chart_range();
        break;

    case 6:
        show_welcome();
        break;
  }

  layer_mark_dirty((Layer *)menu_layer_get_scroll_layer(menu_layer));
}

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(window_layer);

  // s_menulayer_1
  s_menulayer_1 = menu_layer_create(bounds);
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

}

static void destroy_ui(void) {
  window_destroy(s_window);
  menu_layer_destroy(s_menulayer_1);
//  text_layer_destroy(s_textlayer_1);
}

static void handle_menu_window_appear(Window *window) {
  menu_layer_reload_data(s_menulayer_1);
}

static void handle_window_unload(Window* window) {
  destroy_ui();
}

void show_settings(void) {
  initialise_ui();

  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
    .appear = handle_menu_window_appear,
  });

  window_stack_push(s_window, true);
}

void hide_settings(void) {
  window_stack_remove(s_window, true);
}
