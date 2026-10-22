#pragma once
#include <pebble.h>

typedef enum PinType { MEASUREMENT = 0, REMINDER = 1 } PinType;
typedef enum PinCommand { DELETE = 0, ADD = 1 } PinCommand;

typedef struct PinData {
  PinCommand pcommand;
  PinType ptype;
  uint32_t pid;
  uint32_t pdate;
  uint32_t ptime;
  char pweight[21];
} PinData;

void delete_pin(PinType pin_type, uint32_t pin_id);
void add_pin(PinType pin_type, uint32_t pin_id, uint32_t pin_date, uint32_t pin_time, const char* pin_weight);
void pin_initialize();
void pin_deinitalize();

void pin_outbox_sent(DictionaryIterator *iter, void *context);
void pin_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context);
