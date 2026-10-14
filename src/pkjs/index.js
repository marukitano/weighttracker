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
    // Render controls explicitly: Clay text elements are not reliable DOM hosts.
    var csv = page.meta.userData.csv || '';
    var container = document.createElement('div');
    container.style.padding = '16px';
    container.style.background = '#fff';
    container.style.margin = '10px 0';
    var heading = document.createElement('h3');
    heading.textContent = 'Export measurements';
    container.appendChild(heading);
    var status = document.createElement('p');
    var area = document.createElement('textarea');
    area.style.cssText = 'display:none;width:100%;height:160px;box-sizing:border-box';
    var download = document.createElement('button');
    download.type = 'button';
    download.textContent = 'Download CSV';
    download.style.marginRight = '8px';
    var copy = document.createElement('button');
    copy.type = 'button';
    copy.textContent = 'Show / copy CSV';
    status.textContent = csv ?
      (csv.split('\n').length - 2) + ' measurements ready.' :
      'No readings received. Open WeightTracker on the watch, then reopen these settings.';
    download.disabled = !csv;
    copy.disabled = !csv;
    area.value = csv;
    download.addEventListener('click', function() {
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
      status.textContent = 'CSV selected. Copy this text if downloading is blocked.';
    });
    container.appendChild(status);
    container.appendChild(download);
    container.appendChild(copy);
    container.appendChild(area);
    // Insert ahead of the Save button, without interfering with Clay settings.
    var submit = document.querySelector('button[type="submit"],input[type="submit"]');
    if (submit && submit.parentNode) submit.parentNode.insertBefore(container, submit);
    else document.body.appendChild(container);
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
  }, 5000);
  Pebble.sendAppMessage({EXPORT_REQUEST: 1}, function() {}, function() {
    exporting = false;
    if (exportTimer) clearTimeout(exportTimer);
    openSettings();
  });
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event || !event.response || !clay) return;
  var settings;
  try {
    settings = clay.getSettings(event.response);
  } catch (e) {
    console.log('Unable to parse WeightTracker settings: ' + e);
    return;
  }
  // Clay already converts the select and slider values to numeric AppMessage
  // keys. Passing the result through avoids silently dropping settings.
  console.log('Settings payload: ' + JSON.stringify(settings));
  Pebble.sendAppMessage(settings, function() {
    console.log('WeightTracker settings delivered');
  }, function(error) {
    console.log('WeightTracker settings failed: ' + JSON.stringify(error));
  });
});
