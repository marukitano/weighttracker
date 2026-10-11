#pragma once
#include <pebble.h>
#include "../support/utils.h"
#include "../support/config.h"
#include "../support/pin.h"

typedef struct DataPoint {
  uint32_t date;
  double weight;
  int measured;
} DataPoint;

int has_used_already(void);
double get_latest_weight(void);
uint32_t get_latest_date(void);
int get_measured(int i);
uint32_t get_date(int i);
double get_weight(int i);
void load_data(void);
void save_data(void);
void set_current(double weight);
void move_back(void);
int get_num_of_days(void);
double get_current_version();
double get_stored_version();
int32_t get_current_pin_id(PinType pt);
int32_t get_next_pin_id(PinType pt);

#if __TESTING == 1
void test_data(void);
void show_versions(void);
#define LOG_VERSION() show_versions();
#else
#define LOG_VERSION()
#endif

int get_chart_days(void);
void toggle_chart_days(void);
