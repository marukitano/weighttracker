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
    ['LABEL_ABOVE', 'LABEL_BELOW'].forEach(function(key, index) {
      var value = raw[key];
      if (value && typeof value === 'object') value = value.value;
      if (value !== undefined && value !== null) {
        settings[index === 0 ? 65 : 66] = parseInt(value, 10);
      }
    });
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
