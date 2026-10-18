# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1] - unreleased

### Added

- Virtual Xbox One wheel added to Azimuth's controller list, fed by any DirectInput wheel.
- Interpreter for the game's force feedback programs; forces sent through a DirectInput constant force.
- Exclusive foreground access for forces, released and taken back with the game's focus.
- `fh2wheel-config.exe`: axes, buttons, inversion, gain, rate and rotation setup, force tests.
- `fh2wheel-probe.exe`: DirectInput diagnostics.

### Fixed

- ExtraOptions 0.2.88+ refused to start ("another mod owns Azimuth's LoadLibraryExW import") and turned
  every extra option off. fh2wheel now waits for the game image and `azimuth-tls.dll` instead of hooking
  that import.
