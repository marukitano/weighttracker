# WeightTracker

Simple, standalone pebble app to record, monitor and track weight loss (or gain) in the last 14 days period. Supports daily reminders and timeline pins. Both unit of measurement (kilograms, stones, pounds) and date format can be configured.

Pebble Appstore link: https://apps.getpebble.com/applications/54c1918d146d67002f000096

## Pebble Time 2 / SDK 4

This fork adds the `emery` target for the Pebble Time 2 (200 x 228 pixels)
and a responsive chart layout. The project uses SDK 4 `package.json`
metadata instead of the deprecated `appinfo.json`.

Clone and build with a Pebble SDK installation supporting `emery`:

```bash
git clone git@github.com:marukitano/weighttracker-Pebble-time-2.git
cd weighttracker-Pebble-time-2
pebble build
```

The SDK migration has not yet been verified by a local emulator or hardware build.
The original WeightTracker behavior, data format and app UUID have been retained.
