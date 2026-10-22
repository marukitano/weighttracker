#include <pebble.h>
#include "../support/sync.h"
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/data.h"
#include "../support/parameters.h"
#include "input.h"

// Touch ruler based on the vertical selection interaction in Swiss Chronograph.
// Internally values are integer tenths in the currently selected unit.
#define STEP_PX 12
#define VALUE_MIN 1
#define VALUE_MAX 50000
static Window *s_window;
static Layer *s_canvas;
static int s_edit_offset = -1;
static int s_tenths;
static int s_drag_pixels;
static bool s_touching;
static int s_last_y;
static bool s_touch_registered;
static AppTimer *s_roll_timer;
static int s_roll_offset;
static AppTimer *s_fling_timer;
static int32_t s_velocity_q8;
static int32_t s_fling_fraction_q8;
static AppTimer *s_button_intro_timer;
static AppTimer *s_button_bounce_timer;
static uint8_t s_button_bounce_frame;
static int8_t s_button_bounce_x = 30;
static const int8_t BUTTON_BOUNCE_X[] = {30, 18, 8, -3, 2, -1, 0};
static AppTimer *s_button_press_timer;
static int8_t s_button_active = -1; // 0=Select, 1=Down
static bool s_button_held;
static uint8_t s_button_width[2] = {5, 5};
static uint8_t s_delete_frames = 0;
#define DELETE_HOLD_FRAMES 60
#define DELETE_MAX_WIDTH 34

static int clamp_value(int value) {
  if (value < VALUE_MIN) return VALUE_MIN;
  if (value > VALUE_MAX) return VALUE_MAX;
  return value;
}

static void refresh(void) {
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void button_bounce_tick(void *context) {
  s_button_bounce_timer = NULL;
  if ((size_t)(s_button_bounce_frame + 1) < sizeof(BUTTON_BOUNCE_X)) {
    ++s_button_bounce_frame;
    s_button_bounce_x = BUTTON_BOUNCE_X[s_button_bounce_frame];
    refresh();
    if ((size_t)(s_button_bounce_frame + 1) < sizeof(BUTTON_BOUNCE_X))
      s_button_bounce_timer = app_timer_register(42, button_bounce_tick, NULL);
  }
}

static void button_intro_tick(void *context) {
  s_button_intro_timer = NULL;
  s_button_bounce_frame = 0;
  s_button_bounce_x = BUTTON_BOUNCE_X[0];
  refresh();
  s_button_bounce_timer = app_timer_register(42, button_bounce_tick, NULL);
}

static void roll_tick(void *context) {
  s_roll_timer = NULL;
  if (s_roll_offset == 0) return;
  int d = -s_roll_offset * 48 / 100;
  if (!d) d = s_roll_offset > 0 ? -1 : 1;
  s_roll_offset += d;
  if (s_roll_offset > -2 && s_roll_offset < 2) s_roll_offset = 0;
  refresh();
  if (s_roll_offset)
    s_roll_timer = app_timer_register(16, roll_tick, NULL);
}

static void apply_drag(int pixels) {
  s_drag_pixels += pixels;
  while (s_drag_pixels >= STEP_PX && s_tenths < VALUE_MAX) {
    ++s_tenths;
    s_drag_pixels -= STEP_PX;
  }
  while (s_drag_pixels <= -STEP_PX && s_tenths > VALUE_MIN) {
    --s_tenths;
    s_drag_pixels += STEP_PX;
  }
  if ((s_tenths == VALUE_MAX && s_drag_pixels > 0) ||
      (s_tenths == VALUE_MIN && s_drag_pixels < 0))
    s_drag_pixels = 0;
  refresh();
}

static void finish_drag(void) {
  s_roll_offset = s_drag_pixels;
  s_drag_pixels = 0;
  if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
  if (s_roll_offset) s_roll_timer = app_timer_register(16, roll_tick, NULL);
  refresh();
}

static void fling_tick(void *context) {
  s_fling_timer = NULL;
  s_fling_fraction_q8 += s_velocity_q8;
  int movement = (int)(s_fling_fraction_q8 / 256);
  s_fling_fraction_q8 -= movement * 256;
  if (movement) apply_drag(movement);
  s_velocity_q8 = s_velocity_q8 * 92 / 100;
  if ((s_velocity_q8 > -128 && s_velocity_q8 < 128) ||
      (s_tenths == VALUE_MIN && s_velocity_q8 < 0) ||
      (s_tenths == VALUE_MAX && s_velocity_q8 > 0)) {
    finish_drag();
    return;
  }
  s_fling_timer = app_timer_register(28, fling_tick, NULL);
  if (!s_fling_timer) finish_drag();
}

#if PBL_TOUCH
static void on_touch(const TouchEvent *event, void *context) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      if (s_fling_timer) { app_timer_cancel(s_fling_timer); s_fling_timer = NULL; }
      if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
      s_touching = true;
      s_last_y = event->y;
      s_drag_pixels = s_roll_offset;
      s_roll_offset = 0;
      s_velocity_q8 = 0;
      break;
    case TouchEvent_PositionUpdate:
      if (!s_touching) break;
      {
        int delta = event->y - s_last_y;
        s_last_y = event->y;
        s_velocity_q8 = (s_velocity_q8 + delta * 256 * 3) / 4;
        apply_drag(delta);
      }
      break;
    case TouchEvent_Liftoff:
      s_touching = false;
      if (s_velocity_q8 > 9216) s_velocity_q8 = 9216;
      if (s_velocity_q8 < -9216) s_velocity_q8 = -9216;
      s_fling_fraction_q8 = 0;
      if (s_velocity_q8 > 1024 || s_velocity_q8 < -1024) {
        s_fling_timer = app_timer_register(28, fling_tick, NULL);
        if (!s_fling_timer) finish_drag();
      } else finish_drag();
      break;
    default: break;
  }
}
#endif

// Same custom 5x7 pixel glyphs as the rotated chart labels.
static const uint8_t s_weight_glyphs[11][7] = {
  {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
  {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
  {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
  {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
  {0,0,0,0,0,12,12}
};

static void draw_pixel_weight(GContext *ctx, const char *value, int width) {
  const int scale = 4;
  int count = 0, pixels = 0;
  while (value[count] && count < 20) {
    pixels += value[count] == '.' ? 4 : 6;
    ++count;
  }
  int x = (width - pixels * scale) / 2;
  const int y = 9;
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int c = 0; c < count; ++c) {
    int glyph = value[c] == '.' ? 10 : value[c] - '0';
    if (glyph < 0 || glyph > 10) continue;
    for (int row = 0; row < 7; ++row)
      for (int col = 0; col < 5; ++col)
        if (s_weight_glyphs[glyph][row] & (1 << (4 - col)))
          graphics_fill_rect(ctx,
            GRect(x + col * scale, y + row * scale, scale, scale),
            0, GCornerNone);
    x += (value[c] == '.' ? 4 : 6) * scale;
  }
}

// Pixel-art trash can. Every drawn pixel changes from red to white as
// the expanding red delete tab reaches its column.
static void draw_trash_icon(GContext *ctx, int x, int y, int red_edge) {
  // 12-pixel-wide mirrored bitmap: balanced lid, handle and bin walls.
  static const uint16_t rows[15] = {
    0x0F0, 0x0F0, 0x3FC, 0x7FE, 0x7FE,
    0x30C, 0x36C, 0x36C, 0x36C, 0x36C,
    0x36C, 0x36C, 0x36C, 0x3FC, 0x3FC
  };
  for (int row = 0; row < 15; ++row) {
    for (int col = 0; col < 12; ++col) {
      if (!(rows[row] & (1 << (11 - col)))) continue;
      int px = x + col;
      graphics_context_set_fill_color(ctx, px >= red_edge ? GColorWhite : GColorRed);
      graphics_fill_rect(ctx, GRect(px, y + row, 1, 1), 0, GCornerNone);
    }
  }
}

// Pixel letters for the editor mode: rotate the complete word clockwise,
// so NEW / EDIT reads from top to bottom and stays centered on the screen.
static const uint8_t s_mode_letters[6][7] = {
  {31,16,16,30,16,16,31}, // E
  {30,17,17,17,17,17,30}, // D
  {31,4,4,4,4,4,31},       // I
  {31,4,4,4,4,4,4},        // T
  {17,25,21,19,17,17,17}, // N
  {17,17,17,21,21,21,10}  // W
};

static int mode_letter_index(char c) {
  switch (c) {
    case 'E': return 0;
    case 'D': return 1;
    case 'I': return 2;
    case 'T': return 3;
    case 'N': return 4;
    case 'W': return 5;
    default: return -1;
  }
}

static void draw_mode_label(GContext *ctx, int screen_width, int screen_height) {
  const char *label = s_edit_offset >= 0 ? "EDIT" : "NEW";
  const int scale = 4;
  const int letter_pitch = 6 * scale;
  const int length = s_edit_offset >= 0 ? 4 : 3;
  const int x0 = (screen_width - 7 * scale) / 2;
  // Center within the area BELOW the large weight digits (y=9..36),
  // rather than within the entire screen.
  const int weight_bottom = 9 + 7 * 4;
  const int y0 = weight_bottom + (screen_height - weight_bottom - length * letter_pitch) / 2;

  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int c = 0; c < length; ++c) {
    int index = mode_letter_index(label[c]);
    if (index < 0) continue;
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (!(s_mode_letters[index][row] & (1 << (4 - col)))) continue;
        // Rotated 90 degrees clockwise; individual letters stay readable
        // when the watch is turned sideways.
        graphics_fill_rect(ctx,
          GRect(x0 + (6 - row) * scale, y0 + c * letter_pitch + col * scale,
                scale, scale), 0, GCornerNone);
      }
    }
  }
}

static void draw_canvas(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int ruler_x = 0;
  const int readout_y = 23;  // Center of 5x7 pixel digits at y=9, scale 4.
  int cy = bounds.size.h / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);

  // The marker stays still while the ruler ticks roll beneath it.
  for (int step = -22; step <= 22; ++step) {
    int value = s_tenths + step;
    if (value <= 0) continue;
    int y = readout_y - step * STEP_PX + (s_touching || s_fling_timer ? s_drag_pixels : s_roll_offset);
    if (y < 0 || y >= bounds.size.h) continue;
    // 0.1: short thin tick; 0.5: medium thick; 1.0: twice the 0.5 length.
    bool whole = value % 10 == 0;
    bool half = value % 5 == 0;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, half ? 2 : 1);
    graphics_draw_line(ctx, GPoint(ruler_x, y),
                       GPoint(ruler_x + (whole ? 32 : half ? 16 : 9), y));
  }
  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(ruler_x, readout_y),
                     GPoint(ruler_x + 32, readout_y));
  char weight[24];
  snprintf(weight, sizeof(weight), "%d.%d", s_tenths / 10, s_tenths % 10);
  draw_pixel_weight(ctx, weight, bounds.size.w);
  draw_mode_label(ctx, bounds.size.w, bounds.size.h);

  // Main-screen-style spring entrance, 5x52 tabs on the right edge.
  if (s_button_bounce_x < 30) {
    const int shift = s_button_bounce_x;
    const int edge_x = bounds.size.w;
    const int center_y = bounds.size.h / 2;
    const int bottom_y = bounds.size.h - 36;
    const int middle_width = s_button_width[0] + (shift > 0 ? shift : 0);
    const int bottom_width = s_button_width[1] + (shift > 0 ? shift : 0);
    if (s_edit_offset >= 0) {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx,
        GRect(edge_x - middle_width, center_y - 26, middle_width, 52),
        5, GCornersLeft);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(edge_x - 23 + shift, center_y),
                         GPoint(edge_x - 18 + shift, center_y + 5));
      graphics_draw_line(ctx, GPoint(edge_x - 18 + shift, center_y + 5),
                         GPoint(edge_x - 10 + shift, center_y - 6));
    }

    // New: bottom check/save. Edit: bottom red X, hold for 3 seconds.
    GColor action_color = s_edit_offset >= 0 ? GColorRed : GColorBlack;
    graphics_context_set_fill_color(ctx, action_color);
    graphics_fill_rect(ctx,
      GRect(edge_x - bottom_width, bottom_y - 26, bottom_width, 52),
      5, GCornersLeft);
    graphics_context_set_stroke_color(ctx, action_color);
    graphics_context_set_stroke_width(ctx, 2);
    if (s_edit_offset >= 0) {
      int red_edge = edge_x - bottom_width;
      draw_trash_icon(ctx, edge_x - 24 + shift, bottom_y - 8, red_edge);
    } else {
      graphics_draw_line(ctx, GPoint(edge_x - 23 + shift, bottom_y),
                         GPoint(edge_x - 18 + shift, bottom_y + 5));
      graphics_draw_line(ctx, GPoint(edge_x - 18 + shift, bottom_y + 5),
                         GPoint(edge_x - 10 + shift, bottom_y - 6));
    }
  }
}

static void save_weight(void) {
  double weight = (double)s_tenths / 10.0;
  if (s_edit_offset >= 0)
    update_history_weight(s_edit_offset, from_current_unit(weight));
  else
    set_current(from_current_unit(weight));
  window_stack_pop(true);
}

static void button_press_tick(void *context) {
  s_button_press_timer = NULL;
  if (s_button_active < 0) return;
  int button = s_button_active;

  if (s_edit_offset >= 0 && button == 1) {
    // Releasing early cancels the delete. The growing red tab is the timer.
    if (!s_button_held) {
      s_button_width[1] = 5;
      s_delete_frames = 0;
      s_button_active = -1;
      refresh();
      return;
    }
    if (s_delete_frames < DELETE_HOLD_FRAMES) ++s_delete_frames;
    s_button_width[1] = 5 +
      (uint8_t)((DELETE_MAX_WIDTH - 5) * s_delete_frames / DELETE_HOLD_FRAMES);
    refresh();
    if (s_delete_frames >= DELETE_HOLD_FRAMES) {
      s_button_active = -1;
      vibes_short_pulse();
      delete_history_weight(s_edit_offset);
      window_stack_pop(true);
    } else {
      s_button_press_timer = app_timer_register(50, button_press_tick, NULL);
    }
    return;
  }

  // Same press/release pulse as the main screen's right-hand buttons.
  if (s_button_held) {
    if (s_button_width[button] < 7) ++s_button_width[button];
    refresh();
    if (s_button_width[button] < 7)
      s_button_press_timer = app_timer_register(55, button_press_tick, NULL);
  } else if (s_button_width[button] > 5) {
    --s_button_width[button];
    refresh();
    s_button_press_timer = app_timer_register(55, button_press_tick, NULL);
  } else {
    s_button_active = -1;
    save_weight();
  }
}

static void button_raw_down(ClickRecognizerRef recognizer, void *context) {
  ButtonId id = click_recognizer_get_button_id(recognizer);
  int button = id == BUTTON_ID_SELECT ? 0 : id == BUTTON_ID_DOWN ? 1 : -1;
  // New has only the bottom save action.
  if (button < 0 || (s_edit_offset < 0 && button == 0) ||
      s_button_active >= 0 || s_button_bounce_x != 0) return;
  s_button_active = button;
  s_button_held = true;
  s_delete_frames = 0;
  if (s_button_press_timer) app_timer_cancel(s_button_press_timer);
  s_button_press_timer = app_timer_register(
    (s_edit_offset >= 0 && button == 1) ? 50 : 55,
    button_press_tick, NULL);
}

static void button_raw_up(ClickRecognizerRef recognizer, void *context) {
  ButtonId id = click_recognizer_get_button_id(recognizer);
  int button = id == BUTTON_ID_SELECT ? 0 : id == BUTTON_ID_DOWN ? 1 : -1;
  if (button < 0 || button != s_button_active) return;
  s_button_held = false;
  if (s_button_press_timer) app_timer_cancel(s_button_press_timer);
  s_button_press_timer = app_timer_register(
    (s_edit_offset >= 0 && button == 1) ? 1 : 55,
    button_press_tick, NULL);
}

static void click_config(void *context) {
  if (s_edit_offset >= 0)
    window_raw_click_subscribe(BUTTON_ID_SELECT, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, button_raw_down, button_raw_up, NULL);
}

static void on_appear(Window *window) {
  if (!s_button_intro_timer && !s_button_bounce_timer && s_button_bounce_x == 30)
    s_button_intro_timer = app_timer_register(500, button_intro_tick, NULL);
#if PBL_TOUCH
  if (touch_service_is_enabled() && !s_touch_registered) {
    touch_service_subscribe(on_touch, NULL);
    s_touch_registered = true;
  }
#endif
}
static void on_disappear(Window *window) {
#if PBL_TOUCH
  if (s_touch_registered) {
    touch_service_unsubscribe();
    s_touch_registered = false;
  }
#endif
}
static void on_unload(Window *window) {
  sync_set_editor_open(false);
  on_disappear(window);
  if (s_button_intro_timer) { app_timer_cancel(s_button_intro_timer); s_button_intro_timer = NULL; }
  if (s_button_bounce_timer) { app_timer_cancel(s_button_bounce_timer); s_button_bounce_timer = NULL; }
  if (s_button_press_timer) { app_timer_cancel(s_button_press_timer); s_button_press_timer = NULL; }
  s_button_active = -1;
  if (s_roll_timer) { app_timer_cancel(s_roll_timer); s_roll_timer = NULL; }
  if (s_fling_timer) { app_timer_cancel(s_fling_timer); s_fling_timer = NULL; }
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_canvas = NULL;
  s_window = NULL;
}

static void open_editor(int edit_offset) {
  sync_set_editor_open(true);
  s_edit_offset = edit_offset;
  s_drag_pixels = 0;
  s_roll_offset = 0;
  s_touching = false;
  s_button_bounce_x = 30;
  s_button_bounce_frame = 0;
  s_button_active = -1;
  s_button_held = false;
  s_button_width[0] = s_button_width[1] = 5;
  s_delete_frames = 0;
  double initial = edit_offset >= 0
    ? get_history_weight(edit_offset) : get_latest_weight();
  if (initial <= 0) initial = 80.0;
  double current_unit = to_current_unit(initial);
  s_tenths = clamp_value((int)(current_unit * 10 + 0.5));
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  Layer *root = window_get_root_layer(s_window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw_canvas);
  layer_add_child(root, s_canvas);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .appear = on_appear,
    .disappear = on_disappear,
    .unload = on_unload
  });
  window_stack_push(s_window, true);
}

void show_input(void) { open_editor(-1); }
void show_input_edit(int newest_offset) {
  if (newest_offset < 0 || newest_offset >= get_history_count()) return;
  open_editor(newest_offset);
}
void hide_input(void) { if (s_window) window_stack_remove(s_window, true); }
