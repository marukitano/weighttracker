var Clay = require('@rebble/clay');
var config = require('./config.json');

var rows = [];
var exporting = false;
var exportTimer = null;
var configShown = false;
var clay = null;

function customSettingsPage() {
  var page = this;
  page.on(page.EVENTS.AFTER_BUILD, function() {
    var csv = page.meta.userData.csv || '';
    var status = document.getElementById('wt-export-status');
    var area = document.getElementById('wt-export-csv');
    var download = document.getElementById('wt-export-download');
    var copy = document.getElementById('wt-export-copy');
    if (!status || !area || !download || !copy) return;
    status.textContent = csv ? 'Ready: ' + (csv.split('\n').length - 2) + ' measurements' :
      'No measurements received. Please open WeightTracker on your Pebble and try again.';
    download.disabled = !csv;
    copy.disabled = !csv;
    area.value = csv;
    download.addEventListener('click', function() {
      if (!csv) return;
      // Browsers can save the file. Some Pebble companion WebViews block downloads;
      // in that case use Show / copy CSV.
      var blob = new Blob(['\ufeff' + csv], {type: 'text/csv;charset=utf-8'});
      var url = URL.createObjectURL(blob);
      var link = document.createElement('a');
      link.href = url;
      link.download = 'weighttracker-export.csv';
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      setTimeout(function() { URL.revokeObjectURL(url); }, 15000);
    });
    copy.addEventListener('click', function() {
      area.style.display = 'block';
      area.focus();
      area.select();
      status.textContent = 'CSV selected. Copy the text and save it as weighttracker-export.csv if download is blocked.';
    });
  });
}

function openSettings() {
  if (configShown) return;
  configShown = true;
  var lines = ['Date;Weight (kg)'];
  for (var i = 0; i < rows.length; i++) {
    var parts = rows[i].split(',');
    if (parts.length !== 2) continue;
    var d = String(parts[0]);
    var raw = parseInt(parts[1], 10);
    if (!/^\d{8}$/.test(d) || !isFinite(raw)) continue;
    var day = d.slice(6, 8);
    var month = d.slice(4, 6);
    var year = d.slice(0, 4);
    var whole = Math.floor(raw / 10000);
    var fraction = ('0000' + Math.abs(raw % 10000)).slice(-4).replace(/0+$/, '');
    lines.push(day + '.' + month + '.' + year + ';' + whole + (fraction ? ',' + fraction : ''));
  }
  // All records are preserved (including multiple readings on the same day).
  var csv = rows.length ? lines.join('\r\n') + '\r\n' : '';
  clay = new Clay(config, customSettingsPage, {autoHandleEvents: false, userData: {csv: csv}});
  Pebble.openURL(clay.generateUrl());
}

Pebble.addEventListener('appmessage', function(event) {
  if (!exporting) return;
  var payload = event.payload || {};
  var data = payload.EXPORT_DATA !== undefined ? payload.EXPORT_DATA : payload[71];
  var done = payload.EXPORT_DONE !== undefined ? payload.EXPORT_DONE : payload[72];
  if (data) {
    String(data).split(';').forEach(function(row) { if (row) rows.push(row); });
  }
  if (done !== undefined) {
    exporting = false;
    if (exportTimer) clearTimeout(exportTimer);
    exportTimer = null;
    openSettings();
  }
});

Pebble.addEventListener('showConfiguration', function() {
  rows = [];
  exporting = true;
  configShown = false;
  if (exportTimer) clearTimeout(exportTimer);
  // The watch app must be running to respond. Never display stale cached data.
  exportTimer = setTimeout(function() {
    exporting = false;
    openSettings();
  }, 10000);
  Pebble.sendAppMessage({EXPORT_REQUEST: 1}, function() {}, function() {
    exporting = false;
    if (exportTimer) clearTimeout(exportTimer);
    openSettings();
  });
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event || !event.response || !clay) return;
  var settings = clay.getSettings(event.response);
  Pebble.sendAppMessage(settings);
});
