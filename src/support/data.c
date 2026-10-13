#include <pebble.h>
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/data.h"
#include "../support/parameters.h"
#include "../support/reminder.h"
#include "../support/pin.h"

// constants
#define NUM_OF_DAYS 30
#define CHART_RANGE_KEY 13
#define CHART_MODE_KEY 14
#define HISTORY_COUNT_KEY 19
#define HISTORY_CHUNK_KEY 20
#define HISTORY_CHUNK_SIZE 20
#define HISTORY_CHUNKS 5
#define LAST_STORED_DATE_KEY 0
#define UNIT_KEY 1
#define FORMAT_KEY 2
#define REMINDER_FLAG_KEY 3
#define REMINDER_DATA_KEY 4
#define REMINDER_WAKEUP_KEY 6
#define VERSION_KEY 7
#define LAST_MEASUREMENT_WEIGHT_KEY 8
#define LAST_MEASUREMENT_DATE_KEY 9
#define PIN_REMINDER_ID 10
#define PIN_MEASUREMENT_ID 11
#define FIRST_DAY_KEY 12
#define TODAY_INDEX (NUM_OF_DAYS - 1)
#define WT_CURRENT_VERSION 1.2

// global variables
static int s_chart_range = 10;
static int s_chart_mode = 0;
int get_chart_days(void) { return s_chart_range; }
int get_chart_range(void) { return s_chart_range; }
int get_chart_mode(void) { return s_chart_mode; }
void toggle_chart_mode(void) {
  s_chart_mode = 1 - s_chart_mode;
  persist_write_int(CHART_MODE_KEY, s_chart_mode);
}
void next_chart_range(void) {
  s_chart_range = s_chart_range == 10 ? 15 : s_chart_range == 15 ? 30 : 10;
  persist_write_int(CHART_RANGE_KEY, s_chart_range);
}
void toggle_chart_days(void) { next_chart_range(); }

// Compact, durable archive of the last 100 distinct measurement dates.
// Stored in five 160-byte chunks to stay below Pebble's per-key limit.
typedef struct {
  uint32_t date;
  int32_t weight_10000;
} HistoryEntry;
static HistoryEntry s_history[HISTORY_CAPACITY]; // oldest first
static int s_history_count = 0;

int get_history_count(void) { return s_history_count; }
double get_history_weight(int newest_offset) {
  int i = s_history_count - 1 - newest_offset;
  return i >= 0 && i < s_history_count ? (double)s_history[i].weight_10000 / 10000.0 : -1;
}
uint32_t get_history_date(int newest_offset) {
  int i = s_history_count - 1 - newest_offset;
  return i >= 0 && i < s_history_count ? s_history[i].date : 0;
}
static void history_add(uint32_t date, double weight) {
  if (!date || weight <= 0) return;
  int32_t value = (int32_t)(weight * 10000.0 + 0.5);
  int index = 0;
  while (index < s_history_count && s_history[index].date < date) ++index;
  if (index < s_history_count && s_history[index].date == date) {
    s_history[index].weight_10000 = value;
    return;
  }
  if (s_history_count == HISTORY_CAPACITY) {
    if (index == 0) return; // Older than the oldest retained measurement.
    for (int j = 1; j < s_history_count; ++j) s_history[j - 1] = s_history[j];
    --s_history_count;
    --index;
  }
  for (int j = s_history_count; j > index; --j) s_history[j] = s_history[j - 1];
  s_history[index].date = date;
  s_history[index].weight_10000 = value;
  ++s_history_count;
}
static void history_load(void) {
  s_history_count = 0;
  int count = persist_exists(HISTORY_COUNT_KEY) ? persist_read_int(HISTORY_COUNT_KEY) : 0;
  if (count < 0 || count > HISTORY_CAPACITY) return;
  for (int chunk = 0; chunk < HISTORY_CHUNKS && s_history_count < count; ++chunk) {
    int n = count - s_history_count;
    if (n > HISTORY_CHUNK_SIZE) n = HISTORY_CHUNK_SIZE;
    int got = persist_read_data(HISTORY_CHUNK_KEY + chunk,
                                &s_history[s_history_count], n * sizeof(HistoryEntry));
    if (got != n * (int)sizeof(HistoryEntry)) break;
    s_history_count += n;
  }
}
static void history_save(void) {
  for (int chunk = 0; chunk < HISTORY_CHUNKS; ++chunk) {
    int start = chunk * HISTORY_CHUNK_SIZE;
    if (start >= s_history_count) {
      persist_safe_delete(HISTORY_CHUNK_KEY + chunk);
      continue;
    }
    int n = s_history_count - start;
    if (n > HISTORY_CHUNK_SIZE) n = HISTORY_CHUNK_SIZE;
    persist_write_data(HISTORY_CHUNK_KEY + chunk, &s_history[start],
                       n * sizeof(HistoryEntry));
  }
  persist_write_int(HISTORY_COUNT_KEY, s_history_count);
}

static double s_current_weight;
static uint32_t s_current_date;
static uint32_t s_last_stored_date;
static int s_used_already;
static DataPoint data[NUM_OF_DAYS];
static double previous_version = 0.0;
static double s_latest_weight;
static uint32_t s_latest_date;
static int32_t s_reminder_pin_id;
static int32_t s_measurement_pin_id;

int32_t get_current_pin_id(PinType pt) {
  if (pt == MEASUREMENT) {
    return s_measurement_pin_id;
  } else if (pt == REMINDER) {
    return s_reminder_pin_id;
  }

  return 0;
}

int32_t get_next_pin_id(PinType pt) {
  if (pt == MEASUREMENT) {
    s_measurement_pin_id++;
    return s_measurement_pin_id;
  } else if (pt == REMINDER) {
    s_reminder_pin_id++;
    return s_reminder_pin_id;
  }

  return 0;
}

int has_used_already(void) {
  return s_used_already;
}

double get_latest_weight(void) {
  return s_current_weight;
}

uint32_t get_latest_date(void) {
  return s_current_date;
}

int get_measured(int i) {
  if (i < 0 || i > NUM_OF_DAYS - 1) {
    return 0;
  } else {
    return data[i].measured;
  }
}

uint32_t get_date(int i) {
  if (i < 0 || i > NUM_OF_DAYS - 1) {
    return 0;
  } else {
    return data[i].date;
  }
}

double get_weight(int i) {
  if (i < 0 || i > NUM_OF_DAYS - 1) {
    return -1.0;
  } else {
    return data[i].weight;
  }
}

int get_num_of_days(void) {
  return NUM_OF_DAYS;
}

void load_data(void) {
  s_used_already = 0;
  uint32_t dt;
  int i, hr, unit, format, fday;
  WakeupId wuid;

  dt = get_current_date();
  s_current_weight = -1.0;
  s_current_date = 0;
  s_last_stored_date = 0;
  s_latest_weight = -1.0;

  if (persist_exists(VERSION_KEY)) {
    previous_version = persist_read_double(VERSION_KEY);
  } else {
    previous_version = 0.0;
  }

  if (persist_exists(UNIT_KEY)) {
    unit = (int)persist_read_int(UNIT_KEY);
  } else {
    unit = 0;
  }

  if (persist_exists(FORMAT_KEY)) {
    format = (DateFormat)persist_read_int(FORMAT_KEY);
  } else {
    format = YMD;
  }

  if (persist_exists(FIRST_DAY_KEY)) {
    fday = (int)persist_read_int(FIRST_DAY_KEY);
  } else {
    fday = 1;
  }

  init_settings(unit, format, fday);
  s_chart_mode = persist_exists(CHART_MODE_KEY) ? persist_read_int(CHART_MODE_KEY) : 0;
  if (s_chart_mode != 1) s_chart_mode = 0;
  // Older installations used 15/30 calendar days; new default is 10 measurements.
  s_chart_range = persist_exists(CHART_MODE_KEY) && persist_exists(CHART_RANGE_KEY)
      ? persist_read_int(CHART_RANGE_KEY) : 10;
  if (s_chart_range != 10 && s_chart_range != 15 && s_chart_range != 30) s_chart_range = 10;
  history_load();

  if (persist_exists(REMINDER_FLAG_KEY)) {
    hr = persist_read_int(REMINDER_FLAG_KEY);
  } else {
    hr = 0;
  }

  if (hr && persist_exists(REMINDER_DATA_KEY)) {
    if (persist_read_data(REMINDER_DATA_KEY, get_reminder_ptr(), sizeof(Reminder)) < (int)sizeof(Reminder)) {
      hr = 0;
    }
  } else {
    hr = 0;
  }

  if (hr && persist_exists(REMINDER_WAKEUP_KEY)) {
    wuid = persist_read_int(REMINDER_WAKEUP_KEY);
  } else {
    wuid = -1;
  }

  init_reminders(hr, wuid);

  if (persist_exists(PIN_REMINDER_ID)) {
    s_reminder_pin_id = persist_read_int(PIN_REMINDER_ID);
  } else {
    s_reminder_pin_id = 0;
  }

  if (persist_exists(PIN_MEASUREMENT_ID)) {
    s_measurement_pin_id = persist_read_int(PIN_MEASUREMENT_ID);
  } else {
    s_measurement_pin_id = 0;
  }

  if (persist_exists(LAST_STORED_DATE_KEY)) {
    s_last_stored_date = (uint32_t)persist_read_int(LAST_STORED_DATE_KEY);
    s_used_already = 1;
  }

  if (persist_exists(LAST_MEASUREMENT_WEIGHT_KEY)) {
    s_latest_weight = persist_read_double(LAST_MEASUREMENT_WEIGHT_KEY);
    s_used_already = 1;
  }

  if (persist_exists(LAST_MEASUREMENT_DATE_KEY)) {
    s_latest_date = (uint32_t)persist_read_int(LAST_MEASUREMENT_DATE_KEY);
  }

  /*** load data */
  for (i = NUM_OF_DAYS - 1; i >= 0; i--) {
    if (persist_exists(dt)) {
      data[i].weight = persist_read_double(dt);
      data[i].date = dt;
      data[i].measured = 1;
      s_used_already = 1;

      if (s_current_weight < 0.0) {
        s_current_weight = data[i].weight;
        s_current_date = data[i].date;
      }
    } else {
      data[i].date = dt;
      data[i].weight = -1.0;
      data[i].measured = 0;
    }

    dt = decrease_date(dt);
  }

  // Migrate the legacy last-measurement slot and calendar entries.
  if (s_latest_date && s_latest_weight > 0)
    history_add(s_latest_date, s_latest_weight);
  for (i = 0; i < NUM_OF_DAYS; ++i)
    if (data[i].measured) history_add(data[i].date, data[i].weight);
  if (s_history_count > 0 && s_current_weight < 0) {
    s_current_weight = get_history_weight(0);
    s_current_date = get_history_date(0);
  }
  if (s_current_weight < 0) {
    s_current_weight = (s_latest_weight < 0) ? 80.0 : s_latest_weight;
    s_current_date = (s_latest_weight < 0) ? s_current_date : s_latest_date;
  }
}

void save_data(void) {
  int i;
  uint32_t dt;
  bool hasdate = 0;

  dt = get_current_date();

  persist_write_double(VERSION_KEY, WT_CURRENT_VERSION);
  persist_write_int(UNIT_KEY, get_current_unit_index());
  persist_write_int(FORMAT_KEY, (int32_t)get_date_format());
  persist_write_int(FIRST_DAY_KEY, (int32_t)is_monday_first());
  persist_write_int(REMINDER_FLAG_KEY, (int32_t)get_has_reminders());

  if (get_has_reminders()) {
    persist_write_data(REMINDER_DATA_KEY, get_reminder_ptr(), sizeof(Reminder));
    persist_write_int(REMINDER_WAKEUP_KEY, get_wakeup_id());
  }

  persist_write_int(PIN_REMINDER_ID, s_reminder_pin_id);
  persist_write_int(PIN_MEASUREMENT_ID, s_measurement_pin_id);

  if (s_current_weight > 0.0) {
    persist_write_double(LAST_MEASUREMENT_WEIGHT_KEY, s_current_weight);
    persist_write_int(LAST_MEASUREMENT_DATE_KEY, (int32_t)s_current_date);
  } else {
    persist_safe_delete(LAST_MEASUREMENT_WEIGHT_KEY);
    persist_safe_delete(LAST_MEASUREMENT_DATE_KEY);
  }

  history_save();
  if (!s_used_already) {
    return;
  }

  for (i = NUM_OF_DAYS - 1; i >= 0; i--) {
    if (data[i].measured) {
      if (data[i].date < dt) {
        dt = data[i].date;
        hasdate = 1;
      }

      persist_write_double(data[i].date, data[i].weight);
    } else {
      persist_safe_delete(data[i].date);
    }
  }

  if (hasdate) {
    persist_write_int(LAST_STORED_DATE_KEY, (int32_t)dt);
  } else {
    persist_safe_delete(LAST_STORED_DATE_KEY);
  }

  while (s_last_stored_date	> 0 && s_last_stored_date < dt) {
    dt = decrease_date(dt);
    persist_safe_delete(dt);
  }
}

void set_current(double weight) {
  uint32_t dt, ldt;
  long diff;
  long i, k;
  static char wstr[11];
  static char info[21];
  int32_t mt;

  dt = get_current_date();
  mt = get_current_military_time();
  s_current_weight = weight;
  s_current_date = dt;

  ldt = data[TODAY_INDEX].date;

  if (ldt < dt) {
    diff = days(ldt, dt);

    if (diff >= NUM_OF_DAYS) {
      k = 0;
    } else {
      for (i = 0; i + diff < NUM_OF_DAYS; i++) {
        data[i].date = data[i + diff].date;
        data[i].weight = data[i + diff].weight;
        data[i].measured = data[i + diff].measured;
      }

      k = i;
    }

    for (i = k; i < NUM_OF_DAYS; i++) {
      data[i].date = 0;
      data[i].weight = -1.0;
      data[i].measured = 0;
    }
  }

  s_used_already = 1;
  history_add(dt, weight);
  data[TODAY_INDEX].weight = weight;
  data[TODAY_INDEX].date = dt;
  data[TODAY_INDEX].measured = 1;

  double_str(to_current_unit(get_latest_weight()), wstr, 10);
  snprintf(info, 20, "%s %s", wstr, get_short_unit_name());

  add_pin(MEASUREMENT, get_next_pin_id(MEASUREMENT), dt, mt, info);
}

void move_back(void) {
  int i;

  for (i = 0; i + 1 < NUM_OF_DAYS; i++) {
    data[i].date = data[i + 1].date;
    data[i].weight = data[i + 1].weight;
    data[i].measured = data[i + 1].measured;
  }

  data[TODAY_INDEX].weight = -1.0;
  data[TODAY_INDEX].date = get_current_date();
  data[TODAY_INDEX].measured = 0;
}

#if __TESTING == 1
uint32_t _dd(int n) {
  uint32_t cdt = get_current_date();

  for (int i = 0; i < n; i++) {
    cdt = decrease_date(cdt);
  }

  return cdt;
}

void test_data(void) {
  double d;

  persist_write_double(LAST_MEASUREMENT_WEIGHT_KEY, 98.1);
  persist_write_int(LAST_MEASUREMENT_DATE_KEY, (int32_t)_dd(20));

  // data points
  d = 97.9;
  persist_safe_delete(_dd(13));
  persist_write_double(_dd(13), d);
  d = 97.8;
  persist_safe_delete(_dd(12));
  persist_write_double(_dd(12), d);
  d = 97.6;
  persist_safe_delete(_dd(11));
  persist_write_double(_dd(11), d);
  d = 97.5;
  persist_safe_delete(_dd(10));
  persist_write_double(_dd(10), d);
  d = 97.9;
  persist_safe_delete(_dd(9));
  persist_write_double(_dd(9), d);
  d = 97.8;
  persist_safe_delete(_dd(8));
  persist_write_double(_dd(8), d);
  d = 97.6;
  persist_safe_delete(_dd(7));
  persist_write_double(_dd(7), d);
  d = 97.5;
  persist_safe_delete(_dd(6));
  persist_write_double(_dd(6), d);
  d = 97.3;
  persist_safe_delete(_dd(5));
  persist_write_double(_dd(5), d);
  d = 97.9;
  persist_safe_delete(_dd(4));
  persist_write_double(_dd(4), d);
  d = 97.8;
  persist_safe_delete(_dd(3));
  persist_write_double(_dd(3), d);
  d = 97.6;
  persist_safe_delete(_dd(2));
  persist_write_double(_dd(2), d);
  d = 97.5;
  persist_safe_delete(_dd(1));
  persist_write_double(_dd(1), d);
}

void show_versions(void) {
  static char s1[10], s2[10];

  double_str(previous_version, s1, 10);
  double_str(WT_CURRENT_VERSION, s2, 10);
  LOG("Stored version: %s", s1);
  LOG("Current version: %s", s2);
}
#endif

double get_current_version() {
  return WT_CURRENT_VERSION;
}

double get_stored_version() {
  return previous_version;
}
