#include <pebble.h>
#include "parameters.h"

static char date_format_str[4];

static const UnitType units[NUM_OF_UNITS] = {
  { "kilogram", "kg", 1.0, 1 },
  { "stone", "sts", 6.35029318, 2 },
  { "pound", "lbs", 0.45359237, 1 }
};

static int current_unit = 0;
static DateFormat current_format = YMD;
static int start_with_monday = 1;

DateFormat get_date_format(void) {
  return current_format;
}

int is_monday_first(void) {
  return start_with_monday;
}

void toggle_first_day(void) {
  start_with_monday = 1 - start_with_monday;
}

void set_date_format(DateFormat df) {
  current_format = df;
}

double get_conversion_rate(void) {
  return units[current_unit].conversion_rate;
}

const char* get_long_unit_name(void) {
  return units[current_unit].longname;
}

const char* get_short_unit_name(void) {
  return units[current_unit].shortname;
}

double to_current_unit(double v) {
  return (v / get_conversion_rate());
}

double from_current_unit(double v) {
  return (v * get_conversion_rate());
}

void init_settings(int unit_index, DateFormat format, int first) {
  current_unit = unit_index;
  current_format = format;
  start_with_monday = first;
}

int get_current_unit_index(void) {
  return current_unit;
}

void set_current_unit_index(int cui) {
  current_unit = cui;
}

void next_unit_index() {
  current_unit++;
  if (current_unit >= NUM_OF_UNITS) {
    current_unit = 0;
  }
}

double get_accuracy_multiplier(void) {
  switch (units[current_unit].accuracy) {
    case 1:
      return 10.0;
      break;

    case 2:
      return 100.0;
      break;

    case 3:
      return 1000.0;
      break;

    default:
      return 10.0;
      break;
  }
}

double get_accuracy_step(void) {
  switch (units[current_unit].accuracy) {
    case 1:
      return 0.1;
      break;

    case 2:
      return 0.01;
      break;

    case 3:
      return 0.001;
      break;

    default:
      return 0.1;
      break;
  }
}

void next_format() {
  if (current_format == DMY) {
    current_format = YMD;
  } else {
    current_format = (DateFormat)((int)current_format + 1);
  }
}

char* get_date_format_str(void) {
  switch (get_date_format()) {
    case YMD:
      strcpy(date_format_str, "YMD");
      break;

    case MDY:
      strcpy(date_format_str, "MDY");
      break;

    case DMY:
      strcpy(date_format_str, "DMY");
      break;
  }

  return date_format_str;
}
