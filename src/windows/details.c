#include <pebble.h>
#include "../support/config.h"
#include "../support/data.h"
#include "../support/utils.h"
#include "../support/parameters.h"
#include "../windows/details.h"

#define DETAILS_LENGTH 4800
#define LINE_LENGTH 64
#define DATE_LENGTH 15
#define WEIGHT_LENGTH 10

static const int VERT_SCROLL_TEXT_PADDING = 4;
static const int TEXT_PADDING = 4;
#ifdef PBL_ROUND
static const int SCROLL_LAYER_MARGIN = 32;
#endif
static const int MAX_TEXT_LAYER_SIZE = 3200;

static char details[DETAILS_LENGTH + 1] = "";

static Window *s_window;
static ScrollLayer *s_scrolllayer;
static TextLayer *s_textlayer_1;
//static TextLayer *s_textlayer_2;

static void prepare_content(void) {
  bool first = true;
  int i;
  static char date_s[DATE_LENGTH + 1];
  static char weight_s[WEIGHT_LENGTH + 1];
  static char line[LINE_LENGTH + 1] = "";
  details[0] = '\0';

  // Include measurements older than the legacy 30-day calendar window.
  for (i = 0; i < get_history_count(); ++i) {
    uint32_t date = get_history_date(i);
    double weight = get_history_weight(i);
    if (date == 0 || weight <= 0) continue;
    date_str(date, date_s, DATE_LENGTH);
    double_str(to_current_unit(weight), weight_s, WEIGHT_LENGTH);
    snprintf(line, sizeof(line), "- %s: %s %s",
             date_s, weight_s, get_short_unit_name());

    size_t used = strlen(details);
    size_t needed = strlen(line) + (first ? 0 : 1);
    if (used + needed >= sizeof(details)) break;
    if (!first) strcat(details, "\n");
    strcat(details, line);
    first = false;
  }

  if (first) {
    snprintf(details, sizeof(details), "* No data");
  }
}

// Setup the scroll layer on window load
// We do this here in order to be able to get the max used text size
static void initialise_ui() {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);
  GRect max_text_bounds = GRect(0, 0, bounds.size.w, MAX_TEXT_LAYER_SIZE);

  /*
  // s_textlayer_2
  s_textlayer_2 = text_layer_create(GRect(0, 0, 144, 28));
  text_layer_set_background_color(s_textlayer_2, GColorClear);
  text_layer_set_text_color(s_textlayer_2, FOREGROUND);
  text_layer_set_text(s_textlayer_2, "Details");
  text_layer_set_text_alignment(s_textlayer_2, GTextAlignmentCenter);
  text_layer_set_font(s_textlayer_2, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_textlayer_2);
  */

  // Initialize the scroll layer
#ifdef PBL_ROUND
  s_scrolllayer = scroll_layer_create(GRect(0, SCROLL_LAYER_MARGIN, bounds.size.w, bounds.size.h - 2 * SCROLL_LAYER_MARGIN));
#else
  s_scrolllayer = scroll_layer_create(bounds);
#endif

  // This binds the scroll layer to the window so that up and down map to scrolling
  // You may use scroll_layer_set_callbacks to add or override interactivity
  scroll_layer_set_click_config_onto_window(s_scrolllayer, s_window);

  // Initialize the text layer
  s_textlayer_1 = text_layer_create(max_text_bounds);
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, FOREGROUND);
  text_layer_set_text(s_textlayer_1, details);
#ifdef PBL_ROUND
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
#else
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentLeft);
#endif
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  scroll_layer_add_child(s_scrolllayer, text_layer_get_layer(s_textlayer_1));

  //layer_add_child(window_layer, text_layer_get_layer(s_textlayer_2));
  layer_add_child(window_layer, scroll_layer_get_layer(s_scrolllayer));

#ifdef PBL_ROUND
  //text_layer_enable_screen_text_flow_and_paging(s_textlayer_2, 4);
  text_layer_enable_screen_text_flow_and_paging(s_textlayer_1, 4);
  scroll_layer_set_content_size(s_scrolllayer, text_layer_get_content_size(s_textlayer_1));
  scroll_layer_set_paging(s_scrolllayer, true);
#else
  GSize max_size = text_layer_get_content_size(s_textlayer_1);
  max_size.h = max_size.h + TEXT_PADDING;
  text_layer_set_size(s_textlayer_1, max_size);
  scroll_layer_set_content_size(s_scrolllayer, GSize(bounds.size.w, max_size.h + VERT_SCROLL_TEXT_PADDING));
#endif
}

static void destroy_ui() {
  window_destroy(s_window);
  text_layer_destroy(s_textlayer_1);
  //text_layer_destroy(s_textlayer_2);
  scroll_layer_destroy(s_scrolllayer);
}

static void handle_window_unload(Window* window) {
  destroy_ui();
}

void show_details(void) {
  prepare_content();

  initialise_ui();

  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
  });

  window_stack_push(s_window, true);
}

void hide_details(void) {
  window_stack_remove(s_window, true);
}
