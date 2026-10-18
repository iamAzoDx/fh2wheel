# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.4] - 2026-10-18

### Changed

- New look for `fh2wheel-config.exe`: dark theme and a **Live preview** panel with a wheel that turns with
  yours, the pedals, the engaged gear and the Xbox buttons lighting up as you press them.

### Fixed

- `fh2wheel-config.exe` releases the wheel after each force test instead of keeping it until it is
  closed.

## [1.3] - 2026-10-18

### Added

- Handbrake support. The game has no handbrake axis for wheels, so pulling the handbrake past 30 %
  presses **B**, the game's handbrake button with a wheel (A is the clutch). Another button can be set
  with `HandbrakeButton` under `[Axes]` in `data\fh2wheel.ini`.
- A USB handbrake can be its own device: new **Handbrake** list in `fh2wheel-config.exe`
  (`HandbrakeVid`/`HandbrakePid` in `[Devices]`).
- The trace log shows the handbrake value.

### Changed

- Device lists in `fh2wheel-config.exe`: **Wheel** only shows force feedback devices, and a device chosen
  in one list is hidden from the others. Pedals or a handbrake set to the wheel's own device become
  "None" and their axes are read from the wheel.

## [1.2] - 2026-10-18

### Added

- Shifter support, from the wheel base or from a separate USB device (new **Shifter** list in
  `fh2wheel-config.exe`, `[Shifter]` section and `ShifterVid`/`ShifterPid` in `data\fh2wheel.ini`):
  - H-pattern: gears 1 to 7 and reverse are sent to the game as a real Xbox One wheel shifter;
  - sequential: shift up/down buttons act as the right/left paddles, which keep working.
- The trace log shows the gear and the clutch sent to the game.

### Fixed

- Logitech wheels (G923 and others) report every pedal half-pressed until it is first moved, so the game
  saw both triggers held and menus could ignore the paddles. A pedal now stays released until it moves.

## [1.1] - 2026-10-18

### Fixed

- ExtraOptions 0.2.88+ refused to start ("another mod owns Azimuth's LoadLibraryExW import") and turned
  every extra option off. fh2wheel now waits for the game image and `azimuth-tls.dll` instead of hooking
  that import.

### Changed

- Tested with Azimuth 0.9.10 and ExtraOptions 0.2.98. Start the game with `azimuth.exe`: the FH2 DLC
  Launcher (fh2core) also hooks that import, so ExtraOptions 0.2.88+ refuses to start with it.

## [1.0] - 2026-10-17

### Added

- Virtual Xbox One wheel added to Azimuth's controller list, fed by any DirectInput wheel.
- Interpreter for the game's force feedback programs; forces sent through a DirectInput constant force.
- Exclusive foreground access for forces, released and taken back with the game's focus.
- `fh2wheel-config.exe`: axes, buttons, inversion, gain, rate and rotation setup, force tests.
- `fh2wheel-probe.exe`: DirectInput diagnostics.
