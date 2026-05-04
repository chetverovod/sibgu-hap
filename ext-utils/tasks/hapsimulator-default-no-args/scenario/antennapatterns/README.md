# scenario/antennapatterns

Antenna pattern configuration files.
This may be a regular directory or a symlink.

## Required Items
- `GeoPos.in` with one line: `Latitude Longitude Altitude`.
- One or more beam `.txt` files.

## Beam File Rules
- Filename must contain a numeric Beam ID.
- Line format: `Latitude Longitude Gain_dB`.
- `Gain_dB` can be numeric or `NaN`/`nan`.

## User Notes
- ...
