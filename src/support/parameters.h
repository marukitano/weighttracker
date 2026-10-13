#pragma once
#include <pebble.h>

#define NUM_OF_UNITS 3

typedef enum DateFormat { YMD, MDY, DMY } DateFormat;

typedef struct UnitType {
  char longname[21];
  char shortname[5];
  double conversion_rate;
  int accuracy;
} UnitType;

int is_monday_first(void);
void toggle_first_day(void);
DateFormat get_date_format(void);
void set_date_format(DateFormat df);
double get_conversion_rate(void);
const char* get_long_unit_name(void);
const char* get_short_unit_name(void);
double to_current_unit(double v);
double from_current_unit(double v);
void init_settings(int unit_index, DateFormat format, int first);
int get_current_unit_index(void);
double get_accuracy_multiplier(void);
double get_accuracy_step(void);
char* get_date_format_str(void);
void next_unit_index();
void set_current_unit_index(int cui);
void next_format();
