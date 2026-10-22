#include <pebble.h>
#include "sync.h"
#include "data.h"
#include "pin.h"

#define SYNC_CHUNK_RECORDS 8
#define SYNC_BYTES_PER_RECORD 16

// Phone -> watch: START, DOWNLOAD_BEGIN/CHUNK/END.
// Watch -> phone: UPLOAD_BEGIN/CHUNK/END.
#define SYNC_START 1
#define SYNC_UPLOAD_BEGIN 2
#define SYNC_UPLOAD_CHUNK 3
#define SYNC_UPLOAD_END 4
#define SYNC_DOWNLOAD_BEGIN 5
#define SYNC_DOWNLOAD_CHUNK 6
#define SYNC_DOWNLOAD_END 7

static bool s_phone_ready;
static bool s_editor_open;
static bool s_upload_active;
static bool s_download_active;
static bool s_download_complete;
static bool s_upload_pending;
static bool s_awaiting_download;
static uint8_t s_upload_phase;
static int s_upload_offset;
static int s_upload_total;
static SyncRecord s_upload_records[SYNC_WATCH_RECORD_LIMIT];
static SyncRecord s_download_records[SYNC_WATCH_RECORD_LIMIT];
static int s_download_expected;
static int s_download_count;
static AppTimer *s_retry_timer;
static int s_retry_attempts;
static void retry_upload(void *context);

static void schedule_retry(void) {
  if (s_retry_timer || s_retry_attempts >= 5) return;
  ++s_retry_attempts;
  s_retry_timer = app_timer_register(1200, retry_upload, NULL);
}

static uint32_t read_u32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
       | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u32(uint8_t *bytes, uint32_t value) {
  bytes[0] = value & 255;
  bytes[1] = (value >> 8) & 255;
  bytes[2] = (value >> 16) & 255;
  bytes[3] = (value >> 24) & 255;
}

static void decode_record(const uint8_t *bytes, SyncRecord *record) {
  record->id = read_u32(bytes);
  record->date = read_u32(bytes + 4);
  record->weight_10000 = (int32_t)read_u32(bytes + 8);
  record->revision = read_u32(bytes + 12);
}

static void encode_record(uint8_t *bytes, const SyncRecord *record) {
  write_u32(bytes, record->id);
  write_u32(bytes + 4, record->date);
  write_u32(bytes + 8, (uint32_t)record->weight_10000);
  write_u32(bytes + 12, record->revision);
}

static bool send_upload_packet(uint8_t command) {
  DictionaryIterator *dict;
  if (app_message_outbox_begin(&dict) != APP_MSG_OK || !dict) return false;
  if (dict_write_int32(dict, MESSAGE_KEY_SYNC_COMMAND, command) != DICT_OK)
    return false;
  if (command == SYNC_UPLOAD_BEGIN) {
    if (dict_write_int32(dict, MESSAGE_KEY_SYNC_COUNT, s_upload_total) != DICT_OK)
      return false;
  } else if (command == SYNC_UPLOAD_CHUNK) {
    if (dict_write_int32(dict, MESSAGE_KEY_SYNC_OFFSET, s_upload_offset) != DICT_OK)
      return false;
    uint8_t bytes[SYNC_CHUNK_RECORDS * SYNC_BYTES_PER_RECORD];
    int n = s_upload_total - s_upload_offset;
    if (n > SYNC_CHUNK_RECORDS) n = SYNC_CHUNK_RECORDS;
    for (int i = 0; i < n; ++i)
      encode_record(bytes + i * SYNC_BYTES_PER_RECORD,
                    &s_upload_records[s_upload_offset + i]);
    if (dict_write_data(dict, MESSAGE_KEY_SYNC_DATA,
                        bytes, n * SYNC_BYTES_PER_RECORD) != DICT_OK)
      return false;
  }
  dict_write_end(dict);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void begin_upload(void) {
  if (!s_phone_ready || s_upload_active || s_download_active ||
      s_awaiting_download || s_download_complete || s_editor_open) return;
  s_upload_total = sync_record_count();
  if (s_upload_total < 0 || s_upload_total > SYNC_WATCH_RECORD_LIMIT) return;
  for (int i = 0; i < s_upload_total; ++i)
    sync_get_record(i, &s_upload_records[i]);
  s_upload_offset = 0;
  s_upload_phase = SYNC_UPLOAD_BEGIN;
  s_upload_pending = false;
  s_upload_active = true;
  if (!send_upload_packet(SYNC_UPLOAD_BEGIN)) {
    s_upload_active = false;
    s_upload_pending = true;
    schedule_retry();
  }
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  // Timeline pins share the outbox; their acknowledgements must not
  // advance the measurement transfer.
  if (!dict_find(iter, MESSAGE_KEY_SYNC_COMMAND)) {
    pin_outbox_sent(iter, context);
    if (s_upload_pending) schedule_retry();
    return;
  }
  if (!s_upload_active) return;
  if (s_upload_phase == SYNC_UPLOAD_END) {
    s_upload_active = false;
    s_upload_phase = 0;
    // The phone responds with a complete merged download. Do not
    // upload again until that response is applied.
    return;
  }
  if (s_upload_phase == SYNC_UPLOAD_BEGIN) {
    s_upload_phase = s_upload_total ? SYNC_UPLOAD_CHUNK : SYNC_UPLOAD_END;
  } else if (s_upload_phase == SYNC_UPLOAD_CHUNK) {
    s_upload_offset += SYNC_CHUNK_RECORDS;
    if (s_upload_offset >= s_upload_total) s_upload_phase = SYNC_UPLOAD_END;
  }
  // Mark the expected phone response BEFORE sending END: the phone
  // can begin its download before this outbound ACK callback returns.
  if (s_upload_phase == SYNC_UPLOAD_END) s_awaiting_download = true;
  if (!send_upload_packet(s_upload_phase)) {
    s_awaiting_download = false;
    s_upload_active = false;
    s_upload_pending = true;
    schedule_retry();
  }
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason,
                          void *context) {
  if (!dict_find(iter, MESSAGE_KEY_SYNC_COMMAND)) {
    pin_outbox_failed(iter, reason, context);
    if (s_upload_pending) schedule_retry();
    return;
  }
  // Keep all unsent edits in Pebble persistent storage. Next app launch
  // starts a new full exchange instead of trusting a partial transfer.
  s_upload_active = false;
  s_upload_phase = 0;
  s_upload_pending = true;
  schedule_retry();
  APP_LOG(APP_LOG_LEVEL_WARNING, "History upload failed: %d", reason);
}

static void retry_upload(void *context) {
  s_retry_timer = NULL;
  if (s_upload_pending && !s_upload_active && !s_download_active)
    begin_upload();
}

static void apply_download(void) {
  if (!s_download_complete || s_editor_open) return;
  bool changed = false;
  for (int i = 0; i < s_download_expected; ++i)
    changed |= sync_apply_remote_record(&s_download_records[i]);
  s_download_complete = false;
  s_download_active = false;
  if (changed) {
    sync_finish_remote_merge();
    weighttracker_sync_refresh();
  }
  // Defer a second upload until any outstanding outbox ACK has settled.
  if (s_upload_pending) schedule_retry();
}

void sync_on_inbox(DictionaryIterator *iter) {
  Tuple *command_tuple = dict_find(iter, MESSAGE_KEY_SYNC_COMMAND);
  if (!command_tuple) return;
  int command = command_tuple->value->int32;
  if (command == SYNC_START) {
    s_phone_ready = true;
    s_awaiting_download = false;
    s_retry_attempts = 0;
    if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
    s_upload_pending = true;
    s_download_active = false;
    s_download_complete = false;
    s_upload_active = false;
    begin_upload();
    return;
  }
  if (command == SYNC_DOWNLOAD_BEGIN) {
    Tuple *t = dict_find(iter, MESSAGE_KEY_SYNC_COUNT);
    if (!t) return;
    int count = t->value->int32;
    if (count < 0 || count > SYNC_WATCH_RECORD_LIMIT) return;
    s_awaiting_download = false;
    s_download_expected = count;
    s_download_count = 0;
    s_download_active = true;
    s_download_complete = false;
    return;
  }
  if (command == SYNC_DOWNLOAD_CHUNK) {
    if (!s_download_active || s_download_complete) return;
    Tuple *index = dict_find(iter, MESSAGE_KEY_SYNC_OFFSET);
    Tuple *data = dict_find(iter, MESSAGE_KEY_SYNC_DATA);
    if (!index || !data || data->type != TUPLE_BYTE_ARRAY) return;
    int offset = index->value->int32;
    int len = data->length;
    if (len <= 0 || len % SYNC_BYTES_PER_RECORD) return;
    int n = len / SYNC_BYTES_PER_RECORD;
    if (offset != s_download_count || n > SYNC_CHUNK_RECORDS ||
        offset + n > s_download_expected) {
      s_download_active = false;
      return;
    }
    for (int i = 0; i < n; ++i)
      decode_record(data->value->data + i * SYNC_BYTES_PER_RECORD,
                    &s_download_records[offset + i]);
    s_download_count += n;
    return;
  }
  if (command == SYNC_DOWNLOAD_END) {
    if (!s_download_active || s_download_count != s_download_expected) {
      s_download_active = false;
      return;
    }
    s_download_complete = true;
    apply_download();
  }
}

void sync_notify_local_change(void) {
  s_upload_pending = true;
  if (!s_upload_active && !s_download_active && !s_download_complete)
    begin_upload();
}

void sync_set_editor_open(bool editor_open) {
  s_editor_open = editor_open;
  if (!editor_open) {
    apply_download();
    if (s_upload_pending && !s_download_active && !s_download_complete)
      schedule_retry();
  }
}

void sync_init(void) {
  s_phone_ready = false;
  s_editor_open = false;
  s_upload_active = false;
  s_upload_pending = false;
  s_download_active = false;
  s_download_complete = false;
  s_awaiting_download = false;
  s_retry_timer = NULL;
  s_retry_attempts = 0;
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
}
