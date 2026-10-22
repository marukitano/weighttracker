#include <pebble.h>
#include "../support/pin.h"
#include "../support/config.h"
#include "../support/utils.h"
#include "../support/queue.h"

#define MAX_TRIES 3
#define SLEEP_TIME 500

const uint32_t PIN_DATE = 1;
const uint32_t PIN_TIME = 2;
const uint32_t PIN_ID = 3;
const uint32_t PIN_COMMAND = 4;
const uint32_t PIN_TYPE = 5;
const uint32_t PIN_WEIGHT = 6;

static QueueInstance *qi;
static int tries = 0;
static bool msg_busy = false;
static AppTimer *s_pin_retry_timer;
static int s_pin_begin_attempts;

void process_next_pin(void);

static void pin_retry_tick(void *context) {
  s_pin_retry_timer = NULL;
  if (qi && queue_length(qi) > 0) process_next_pin();
}

static void pin_retry_if_busy(void) {
  // A history chunk may own the one Pebble outbox. Retry once it is free.
  if (!s_pin_retry_timer) s_pin_retry_timer = app_timer_register(650, pin_retry_tick, NULL);
}

#if __TESTING == 1
char *tr_ptype(PinType pt) {
  switch (pt) {
    case MEASUREMENT: return "M";
    case REMINDER: return "R";
    default: return "U";
  }
}

char *tr_pcommand(PinCommand pc) {
  switch (pc) {
    case DELETE: return "D";
    case ADD: return "A";
    default: return "U";
  }
}

void LOG_PIN(PinData *pd) {
  LOG_INFO("*** PIN: {pcommand: %s, ptype: %s, pid: %lu, pdate: %lu, ptime: %lu, pweight: %s}", tr_pcommand(pd->pcommand), tr_ptype(pd->ptype), pd->pid, pd->pdate, pd->ptime, pd->pweight);
}
#else
void LOG_PIN(PinData *pd) { /* empty */ }
#endif

#if __TESTING == 1
char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    case APP_MSG_INVALID_STATE: return "APP_MSG_INVALID_STATE";
    default: return "UNKNOWN ERROR";
  }
}
#endif

void delete_pin_execute(PinData* pd) {
  LOG_INFO("<<< Deleting pin");
  LOG_PIN(pd);
  msg_busy = true;

  DictionaryIterator *iterator;
#if __TESTING == 1
  AppMessageResult result = app_message_outbox_begin(&iterator);
#else
  app_message_outbox_begin(&iterator);
#endif

  if (iterator == NULL) {
    if (++s_pin_begin_attempts < 10) {
      pin_retry_if_busy();
    } else {
      // Do not block the entire pin queue forever when the phone is offline.
      s_pin_begin_attempts = 0;
      queue_remove(qi);
      msg_busy = false;
      process_next_pin();
    }
    return;
  }
  s_pin_begin_attempts = 0;

  dict_write_uint32(iterator, PIN_COMMAND, DELETE);
  dict_write_uint32(iterator, PIN_TYPE, pd->ptype);
  dict_write_uint32(iterator, PIN_ID, pd->pid);
  dict_write_end(iterator);

  app_message_outbox_send();
}

void add_pin_execute(PinData* pd) {
  LOG_INFO(">>> Adding pin");
  LOG_PIN(pd);
  msg_busy = true;

  DictionaryIterator *iterator;
#if __TESTING == 1
  AppMessageResult result = app_message_outbox_begin(&iterator);
#else
  app_message_outbox_begin(&iterator);
#endif

  if (iterator == NULL) {
    if (++s_pin_begin_attempts < 10) {
      pin_retry_if_busy();
    } else {
      // Do not block the entire pin queue forever when the phone is offline.
      s_pin_begin_attempts = 0;
      queue_remove(qi);
      msg_busy = false;
      process_next_pin();
    }
    return;
  }
  s_pin_begin_attempts = 0;

  dict_write_uint32(iterator, PIN_COMMAND, ADD);
  dict_write_uint32(iterator, PIN_TYPE, pd->ptype);
  dict_write_uint32(iterator, PIN_DATE, pd->pdate);
  dict_write_uint32(iterator, PIN_TIME, pd->ptime);
  dict_write_uint32(iterator, PIN_ID, pd->pid);

  if (pd->ptype == MEASUREMENT) {
    dict_write_cstring(iterator, PIN_WEIGHT, pd->pweight);
  }

  dict_write_end(iterator);

  app_message_outbox_send();
}

void process_next_pin() {
  PinData *pd = (PinData*)queue_head(qi);

  if (pd == NULL) {
    msg_busy = false;
  } else {
    msg_busy = true;
    tries = 0;
    if (pd->pcommand == DELETE) {
      delete_pin_execute(pd);
    } else if (pd->pcommand == ADD) {
      add_pin_execute(pd);
    }
  }
}

void pin_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  LOG_ERROR("--- Send failed! (%s)", translate_error(reason));

  if (queue_length(qi) > 0) {
    if (tries < MAX_TRIES) {
      tries++;
      psleep(SLEEP_TIME);
      process_next_pin();
    } else {
      queue_remove(qi);
      process_next_pin();
    }
  }
}

void pin_outbox_sent(DictionaryIterator *iterator, void *context) {
  LOG_INFO("+++ Send success!");
  queue_remove(qi);
  process_next_pin();
}

void pin_initialize() {
  // Sync registers the shared AppMessage callbacks/buffers; pin retains
  // its own queue and is notified only for pin-message acknowledgements.
  qi = queue_create_instance();
}

void pin_deinitalize() {
  if (s_pin_retry_timer) { app_timer_cancel(s_pin_retry_timer); s_pin_retry_timer = NULL; }
  queue_destroy_instance(qi);
  qi = NULL;
}

void delete_pin(PinType pin_type, uint32_t pin_id) {
  LOG("### pin delete -> q");
  PinData *pd = (PinData*)malloc(sizeof(PinData));

  if (pd != NULL) {
    pd->pcommand = DELETE;
    pd->pid = pin_id;
    pd->ptype = pin_type;
    queue_add(qi, (void *)pd);

    if (!msg_busy) {
      process_next_pin();
    }
  }
}

void add_pin(PinType pin_type, uint32_t pin_id, uint32_t pin_date, uint32_t pin_time, const char* pin_weight) {
  LOG("### pin add -> q");
  PinData *pd = (PinData*)malloc(sizeof(PinData));

  if (pd != NULL) {
    pd->pcommand = ADD;
    pd->pid = pin_id;
    pd->ptype = pin_type;
    pd->pdate = pin_date;
    pd->ptime = pin_time;
    strcpy(pd->pweight, pin_weight);
    queue_add(qi, (void *)pd);

    if (!msg_busy) {
      process_next_pin();
    }
  }
}
