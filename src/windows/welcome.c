#include <pebble.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../windows/welcome.h"

#define VERT_SCROLL_TEXT_PADDING 4
#define TEXT_PADDING 34
#define MAX_TEXT_LAYER_SIZE 2000

static const char s_details[] =
"\n"
"WeightTracker v1.2\n"
"(c) Viktor Csala\n"
"\n"
"What's new in version 1.2: showing the first day of the weeks on the graph to enable quicker grasp of it\n"
"\n"
"Short instructions to use the app:\n"
"- UP button on the chart - browse older measurements\n"
"- SELECT button - opens the measurement list\n"
"- DOWN button - browse newer measurements\n"
"\n"
"Configure units, height, chart mode, range and colors on your phone. Press BACK to close this page.";

static Window *s_window;
static ScrollLayer *s_scrolllayer;
static TextLayer *s_textlayer_1;

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, BACKGROUND);
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);

  GRect max_text_bounds = GRect(0, 0, bounds.size.w, MAX_TEXT_LAYER_SIZE);
  s_scrolllayer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_scrolllayer, s_window);

  s_textlayer_1 = text_layer_create(max_text_bounds);
  text_layer_set_background_color(s_textlayer_1, GColorClear);
  text_layer_set_text_color(s_textlayer_1, FOREGROUND);
  text_layer_set_text(s_textlayer_1, s_details);
#ifdef PBL_ROUND
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentCenter);
#else
  text_layer_set_text_alignment(s_textlayer_1, GTextAlignmentLeft);
#endif
  text_layer_set_font(s_textlayer_1, fonts_get_system_font(FONT_KEY_GOTHIC_14));

  scroll_layer_add_child(s_scrolllayer, text_layer_get_layer(s_textlayer_1));

  layer_add_child(window_layer, scroll_layer_get_layer(s_scrolllayer));

#ifdef PBL_ROUND
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

static void destroy_ui(void) {
  window_destroy(s_window);
  text_layer_destroy(s_textlayer_1);
  scroll_layer_destroy(s_scrolllayer);
}

static void handle_window_unload(Window* window) {
  destroy_ui();
}

void show_welcome(void) {
  initialise_ui();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
  });
  window_stack_push(s_window, true);
}

void hide_welcome(void) {
  window_stack_remove(s_window, true);
}
