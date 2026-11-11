require('./sync');
var Clay = require('@rebble/clay');
var config = require('./config.json');
var clay = new Clay(config, null, {autoHandleEvents: false});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event || !event.response || event.response === 'CANCELLED') return;
  var settings;
  try {
    settings = clay.getSettings(event.response);
    // The Clay serializer on some phone versions drops newly introduced
    // select keys. Read their submitted values as a fallback.
    var raw;
    try {
      raw = JSON.parse(decodeURIComponent(event.response));
    } catch (error) {
      raw = typeof event.response === 'object' ? event.response : {};
    }
    var fallbackKeys = {LABEL_ABOVE: 65, LABEL_BELOW: 66, DARK_MODE: 67};
    Object.keys(fallbackKeys).forEach(function(key) {
      var value = raw[key];
      if (value && typeof value === 'object') value = value.value;
      if (value !== undefined && value !== null) {
        var parsed = parseInt(value, 10);
        if (!isNaN(parsed)) settings[fallbackKeys[key]] = parsed;
      }
    });
    // Clay can create a spurious "NaN" entry for a custom select field.
    // Only numerical Pebble message keys are valid; never forward this entry.
    var outgoing = {};
    Object.keys(settings).forEach(function(key) {
      if (/^[0-9]+$/.test(key)) outgoing[key] = settings[key];
    });
    settings = outgoing;
    console.log('Settings submitted: ' + JSON.stringify(settings));
    Pebble.sendAppMessage(settings, function() {
      console.log('WeightTracker settings delivered');
    }, function(error) {
      console.log('Settings delivery failed: ' + JSON.stringify(error));
    });
  } catch (error) {
    console.log('Unable to parse settings: ' + error);
  }
});
