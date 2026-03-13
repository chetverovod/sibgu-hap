# SNS3 Data - Scenarios

This folder contains all the available scenarios created:

- `geo-33E`: see [README](geo-33E/README.md)
- `geo-33E-lora`: see [README](geo-33E-lora/README.md)


More details are given in the README of each scenario.

Each scenario is composed of the following folders:

- `antennapatterns`: symbolic link to the antenna patterns to use. Original folder is located in `data/additional-input/antennapatterns`
- `beams`: forward and return configuration files, used to decribe beams
- `beamhopping`: beam hopping configuration files. Beam hopping is used only if this folder is present
- `positions`: contains positions of GWs and UTs (more UTs can be added via `SimulationHelper` and `GroupHelper`). Contains also description of satellite position: either GEO satellite position, or TLE + list of ISL + simulation start date
- `standard`: standard to use. Can be either DVB or LORA
- `waveforms`: contains list of all waveforms used on return link. It must contains a file `waveforms.txt` that describes all the waveforms, and a file `default_waveform.txt` that contains default waveform ID
