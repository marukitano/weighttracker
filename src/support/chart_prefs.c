#include <pebble.h>
#include "chart_prefs.h"
#include "data.h"
#include "parameters.h"

#define KEY_HEIGHT 60
#define KEY_COLOR 61
#define KEY_MODE 62
#define KEY_RANGE 63
#define KEY_UNIT 64
#define MIN_RANGE 2
#define MAX_RANGE 30

static int s_height_cm;
static int s_color_mode;
int chart_prefs_height_cm(void) { return s_height_cm; }
int chart_prefs_color_mode(void) { return s_color_mode; }
GColor chart_prefs_solid_color(void) {
  switch (s_color_mode) {
    case 2: return GColorDarkGreen;
    case 3: return GColorDarkBlue;
    case 4: return GColorDarkYellow;
    default: return GColorDarkCandyAppleRed;
  }
}
void chart_prefs_load(void) {
  s_height_cm = persist_exists(KEY_HEIGHT) ? persist_read_int(KEY_HEIGHT) : 0;
  if (s_height_cm < 100 || s_height_cm > 230) s_height_cm = 0;
  s_color_mode = persist_exists(KEY_COLOR) ? persist_read_int(KEY_COLOR) : 0;
  if (s_color_mode < 0 || s_color_mode > 4) s_color_mode = 0;
}
static int as_int(Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  return t->value->int32;
}
bool chart_prefs_apply(DictionaryIterator *iter) {
  bool changed = false;
  Tuple *t;
  if ((t = dict_find(iter, KEY_HEIGHT))) {
    int value = as_int(t);
    if (value >= 0 && value <= 230) {
      s_height_cm = value >= 100 ? value : 0;
      persist_write_int(KEY_HEIGHT, s_height_cm);
      changed = true;
    }
  }
  if ((t = dict_find(iter, KEY_COLOR))) {
    int value = as_int(t);
    if (value >= 0 && value <= 4) {
      s_color_mode = value;
      persist_write_int(KEY_COLOR, value);
      changed = true;
    }
  }
  if ((t = dict_find(iter, KEY_MODE))) {
    int value = as_int(t);
    if (value == 0 || value == 1) {
      set_chart_mode(value);
      changed = true;
    }
  }
  if ((t = dict_find(iter, KEY_RANGE))) {
    int value = as_int(t);
    if (value >= MIN_RANGE && value <= MAX_RANGE) {
      set_chart_range(value);
      changed = true;
    }
  }
  if ((t = dict_find(iter, KEY_UNIT))) {
    int value = as_int(t);
    if (value >= 0 && value < NUM_OF_UNITS) {
      set_current_unit_index(value);
      persist_write_int(1, value);
      changed = true;
    }
  }
  return changed;
}
