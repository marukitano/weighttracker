#pragma once
#include <pebble.h>

// Transfer fixed 16-byte measurement records in small acknowledged packets.
// The phone merges both watches; the watch always keeps its own offline copy.
void sync_init(void);
void sync_on_inbox(DictionaryIterator *iter);
void sync_notify_local_change(void);
void sync_set_editor_open(bool editor_open);

// Implemented by the main chart screen.
void weighttracker_sync_refresh(void);
