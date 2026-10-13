#pragma once
#include <pebble.h>
void chart_prefs_load(void);
bool chart_prefs_apply(DictionaryIterator *iter);
int chart_prefs_height_cm(void);
int chart_prefs_color_mode(void);
GColor chart_prefs_solid_color(void);
GColor chart_prefs_line_color(void);
