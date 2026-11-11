#pragma once
#include <pebble.h>
// Shared palette for chart, controls and weight editor.
bool chart_prefs_dark_mode(void);
GColor chart_prefs_background_color(void);
GColor chart_prefs_foreground_color(void);
void chart_prefs_load(void);
bool chart_prefs_apply(DictionaryIterator *iter);
int chart_prefs_height_cm(void);
int chart_prefs_color_mode(void);
GColor chart_prefs_solid_color(void);
GColor chart_prefs_line_color(void);
int chart_prefs_label_above(void);
int chart_prefs_label_below(void);
