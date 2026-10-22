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

#define HISTORY_CAPACITY 100
int get_history_count(void);
double get_history_weight(int newest_offset);
uint32_t get_history_date(int newest_offset);
bool update_history_weight(int newest_offset, double weight);
bool delete_history_weight(int newest_offset);
int get_chart_mode(void); // 0 = measurements, 1 = calendar days
void toggle_chart_mode(void);
void next_chart_range(void);
int get_chart_range(void);
void set_chart_mode(int mode);
void set_chart_range(int count);


// Shared phone/watch history, including deletion markers. Wire format is
// four little-endian 32-bit integers per record (16 bytes).
typedef struct {
  uint32_t id;
  uint32_t date;
  int32_t weight_10000; // 0 means deleted
  uint32_t revision;
} SyncRecord;

#define SYNC_WATCH_RECORD_LIMIT (HISTORY_CAPACITY + 80)
int sync_record_count(void);
bool sync_get_record(int index, SyncRecord *record);
bool sync_apply_remote_record(const SyncRecord *record);
void sync_finish_remote_merge(void);
