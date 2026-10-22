// Phone-side master history shared by both Pebble Time 2 watches.
// Everything stays in the Pebble companion app's localStorage: no server.
var STORAGE_KEY = 'weighttracker.history.sync.v1';
var START = 1;
var UPLOAD_BEGIN = 2;
var UPLOAD_CHUNK = 3;
var UPLOAD_END = 4;
var DOWNLOAD_BEGIN = 5;
var DOWNLOAD_CHUNK = 6;
var DOWNLOAD_END = 7;
var WATCH_LIVE_LIMIT = 100;
var WATCH_TOMBSTONE_LIMIT = 80;
var CHUNK_RECORDS = 8;
var RECORD_SIZE = 16;
var upload = null;
var downloadBusy = false;
var queuedStart = false;
var storageDamaged = false;

function field(dict, name, key) {
  return Object.prototype.hasOwnProperty.call(dict, name) ? dict[name] : dict[key];
}

function loadHistory() {
  var raw = localStorage.getItem(STORAGE_KEY);
  if (!raw) return [];
  try {
    var parsed = JSON.parse(raw);
    if (!parsed || parsed.version !== 1 || !Array.isArray(parsed.records)) {
      throw new Error('Unknown history format');
    }
    for (var i = 0; i < parsed.records.length; ++i) {
      var r = parsed.records[i];
      if (!isValid(r)) throw new Error('Damaged record at ' + i);
    }
    return parsed.records;
  } catch (e) {
    // Never replace potentially recoverable data after a storage error.
    storageDamaged = true;
    console.log('WeightTracker phone history needs recovery: ' + e.message);
    return null;
  }
}

function isValid(r) {
  return r && isFinite(r.id) && r.id > 0 && r.id <= 4294967295 &&
    Math.floor(r.id) === r.id &&
    isFinite(r.revision) && r.revision > 0 && r.revision <= 4294967295 &&
    Math.floor(r.revision) === r.revision &&
    isFinite(r.weight) && Math.floor(r.weight) === r.weight &&
    (r.weight === 0 ||
      (r.weight > 0 && r.date >= 20000101 && r.date <= 20991231));
}

function u32(bytes, offset) {
  return ((bytes[offset] & 255) |
         ((bytes[offset + 1] & 255) << 8) |
         ((bytes[offset + 2] & 255) << 16) |
         ((bytes[offset + 3] & 255) << 24)) >>> 0;
}

function put32(bytes, num) {
  var n = num >>> 0;
  bytes.push(n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255);
}

function fromBytes(bytes, offset) {
  return {
    id: u32(bytes, offset),
    date: u32(bytes, offset + 4),
    weight: u32(bytes, offset + 8) | 0,
    revision: u32(bytes, offset + 12)
  };
}

function toBytes(records) {
  var bytes = [];
  records.forEach(function(r) {
    put32(bytes, r.id);
    put32(bytes, r.date);
    put32(bytes, r.weight);
    put32(bytes, r.revision);
  });
  return bytes;
}

// Highest revision wins. Equal-revision deletes win over edits. The
// remaining tie-break ensures deterministic convergence on both watches.
function shouldReplace(existing, incoming) {
  if (incoming.revision !== existing.revision)
    return incoming.revision > existing.revision;
  if (incoming.weight === 0 && existing.weight !== 0) return true;
  if (existing.weight === 0 && incoming.weight !== 0) return false;
  return incoming.weight > existing.weight;
}

function mergeRecords(existing, incoming) {
  var byId = Object.create(null);
  existing.forEach(function(r) { byId[String(r.id)] = r; });
  incoming.forEach(function(r) {
    var key = String(r.id);
    if (!byId[key] || shouldReplace(byId[key], r)) byId[key] = r;
  });
  return Object.keys(byId).map(function(key) { return byId[key]; });
}

function phoneSnapshot(records) {
  var live = records.filter(function(r) { return r.weight > 0; });
  var dead = records.filter(function(r) { return r.weight === 0; });
  live.sort(function(a, b) {
    return b.date - a.date || b.revision - a.revision || b.id - a.id;
  });
  dead.sort(function(a, b) { return b.revision - a.revision || b.id - a.id; });
  return live.slice(0, WATCH_LIVE_LIMIT).concat(dead.slice(0, WATCH_TOMBSTONE_LIMIT));
}

function send(command, offset, count, bytes, success, failure) {
  var message = {SYNC_COMMAND: command};
  if (typeof offset === 'number') message.SYNC_OFFSET = offset;
  if (typeof count === 'number') message.SYNC_COUNT = count;
  if (bytes) message.SYNC_DATA = bytes;
  Pebble.sendAppMessage(message, success, function(error) {
    console.log('History message ' + command + ' failed: ' + JSON.stringify(error));
    if (failure) failure();
  });
}

function requestUpload() {
  if (storageDamaged) return;
  if (downloadBusy) {
    queuedStart = true;
    return;
  }
  send(START, null, null, null, function() {
    console.log('WeightTracker history sync requested');
  }, function() {
    // A temporary Bluetooth interruption must not modify either archive.
    setTimeout(requestUpload, 4000);
  });
}

function sendDownload(records) {
  var snapshot = phoneSnapshot(records);
  var offset = 0;
  downloadBusy = true;
  function failure() {
    downloadBusy = false;
    // Phone data is already committed. Reconnect and retry the full
    // exchange instead of leaving the other watch with partial history.
    setTimeout(requestUpload, 4000);
  }
  function step() {
    if (offset >= snapshot.length) {
      send(DOWNLOAD_END, null, null, null, function() {
        downloadBusy = false;
        if (queuedStart) {
          queuedStart = false;
          requestUpload();
        }
        console.log('WeightTracker synced ' + snapshot.length + ' records');
      }, failure);
      return;
    }
    var packet = snapshot.slice(offset, offset + CHUNK_RECORDS);
    var currentOffset = offset;
    send(DOWNLOAD_CHUNK, currentOffset, null, toBytes(packet), function() {
      offset += packet.length;
      step();
    }, failure);
  }
  send(DOWNLOAD_BEGIN, null, snapshot.length, null, step, failure);
}

Pebble.addEventListener('ready', function() {
  // Wait until the watch has registered AppMessage callbacks.
  setTimeout(requestUpload, 400);
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload || {};
  var command = field(payload, 'SYNC_COMMAND', 70);
  if (command === undefined || storageDamaged) return;
  command = Number(command);

  if (command === UPLOAD_BEGIN) {
    var expected = Number(field(payload, 'SYNC_COUNT', 72));
    if (!isFinite(expected) || expected < 0 || expected > 180 ||
        Math.floor(expected) !== expected) return;
    upload = { expected: expected, records: [] };
    return;
  }

  if (command === UPLOAD_CHUNK) {
    if (!upload) return;
    var offset = Number(field(payload, 'SYNC_OFFSET', 71));
    var bytes = field(payload, 'SYNC_DATA', 73);
    if (!Array.isArray(bytes) || bytes.length % RECORD_SIZE !== 0 ||
        bytes.length === 0 || bytes.length > CHUNK_RECORDS * RECORD_SIZE ||
        offset !== upload.records.length ||
        offset + bytes.length / RECORD_SIZE > upload.expected) {
      upload = null;
      return;
    }
    for (var i = 0; i < bytes.length; i += RECORD_SIZE) {
      var record = fromBytes(bytes, i);
      if (!isValid(record)) { upload = null; return; }
      upload.records.push(record);
    }
    return;
  }

  if (command === UPLOAD_END) {
    if (!upload || upload.records.length !== upload.expected) {
      upload = null;
      return;
    }
    var original = loadHistory();
    if (!original) return;
    var merged = mergeRecords(original, upload.records);
    upload = null;
    try {
      // Commit the entire merge before telling the watch anything.
      localStorage.setItem(STORAGE_KEY, JSON.stringify({version: 1, records: merged}));
    } catch (error) {
      console.log('Cannot save phone history: ' + error.message);
      return;
    }
    sendDownload(merged);
  }
});
