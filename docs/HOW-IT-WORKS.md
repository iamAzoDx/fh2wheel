# How fh2wheel works

Target: `forza_durango_release_final.exe` 1.0.0.64 (timestamp `565501ED`), studied with Ghidra and
objdump. Addresses below are for that exact build; all of them live in `dll/src/targets.h`.

## Why the wheel is not seen

- Azimuth 0.9.9 only creates `Windows.Xbox.Input.Gamepad` objects, fed by XInput. It loads
  `xinput1_4.dll` with `LOAD_LIBRARY_SEARCH_SYSTEM32`, so an XInput proxy next to the executable is never
  loaded.
- DirectInput wheels do not show up in XInput, so the game receives no controller for them.

## How the game detects a wheel

- Controllers come from `Windows.Xbox.Input.Controller` (statics interface
  `7400624F-144B-47FC-9F3B-1DCAFB482873`): slot 6 `GetControllers`, 7 `ControllerAdded`,
  9 `ControllerRemoved`, 11 `ControllerPairingChanged`.
- When a controller is added (`0x14106d6d0`), the game queries `IGamepad`
  (`082E402C-07DF-45E1-A5AB-A3127AF197B5`), then the **wheel interface**
  (`646979CF-6B71-4E96-8DF9-59E398D7420C`), which Azimuth does not implement.
- The game's XInput-style capabilities function (`0x14106c000`) reports `Type 1 / SubType 2` (wheel) for
  that interface and reads the maximum rotation (900 by default, capped at 900).

## Reading the wheel

The game's XInput-style state function (`0x14106c320`) calls wheel slot 12 to get a “reading” object, then
reads (slot = vtable offset / 8):

| Slot | Content | Game use |
|---|---|---|
| 6–9 | A, B, X, Y | buttons `0x1000`–`0x8000` |
| 10, 11 | View, Menu | `0x20`, `0x10` |
| 12–15 | D-pad up, down, left, right | `0x1`–`0x8` |
| 16 | normalised steering (−1..1) | `sThumbLX` |
| 17 | maximum rotation (degrees) | capabilities |
| 18 / 24 | throttle / present | right trigger |
| 19 / 25 | brake / present | left trigger |
| 20 / 26 | clutch / present | `sThumbRY` |
| 28, 29 | left and right paddles | shoulders |
| 30, 31, 32 | H-shifter present, max gear, gear | `sThumbRX` |
| 33 | 64-bit timestamp | packet number |

Gears: the state function stores slot 32 in `sThumbRX` as is, except `255`, which becomes `-1` (reverse);
`0` is neutral. The capabilities function (`0x14106c000`) calls slot 30 and, when it reports a shifter,
stores slot 31 (max gear) in the capabilities' `sThumbRX`. It tests slot 30's answer as a 32-bit value
on a stack slot that still holds `900`, so a one-byte `false` does not clear it: the game may consider
an H-shifter present even when none is reported. Not verified in game.

## Force feedback is programmable

Xbox One wheels run small **text programs**. The game loads them into the wheel (`0x141071850`), then
updates named variables every frame (`0x141075040`).

| Wheel slot | Role |
|---|---|
| 10 | device id (compared with `0x044FB664`, Thrustmaster TX) |
| 13 | settings object: slots 11/12 min/max rotation, 14 rotation can change |
| 14 | set rotation |
| 26 | load a program (slot, text, option, out) |
| 30 | enable a program |
| 33 | commit |
| 36 | declare a variable |
| 38 | set a variable (`HSTRING`, `float`) |
| 41 | send (called on every update) |
| 42 | reset |

Programs sent by the game:

| # | Role |
|---|---|
| 0 | heartbeat: when `PauseHeartbeat` is set, `L0 = Lt + 2500` |
| 1 | `Af += Torque` while `Lt <= L0` |
| 2 | in-race spring, capped |
| 3 | wheel velocity `L1` |
| 4 | in-race damping, capped |
| 5 | menu spring, only once the heartbeat has expired |
| 6, 7 | left/right square-wave rumble |
| 8 | ±0.5 cap, only for device id `0x044FB664` |

For example, program 1 is `TOFL Lt E0 GT E0 L0 E0 ENDN E0 MOV {Torque} Af+`. Registers: `E0..E3` per
program, `L0..L3` shared, `Lt` time in ms, `Et` time since the program was enabled, `Ad` wheel position,
`Af` output force (`Af+` adds to it). Variables are written `{Name}`.

## What fh2wheel does

1. **Loading** — Azimuth imports `D3DCOMPILER_47.dll`, which is not a KnownDLL. fh2wheel's proxy of that
   file sits next to `azimuth.exe`, forwards all 29 exports (with their ordinals) to the system DLL and
   loads `fh2wheel.dll`.
2. **Hooking** — a thread waits until Azimuth has mapped the game and loaded its TLS module (no import of
   `azimuth.exe` is touched), checks the game fingerprint and the expected bytes, then chains the game's import slot for `GetActivationFactoryByPCWSTR`
   (`0x141DD1330`). Other mods hooking the same slot keep working.
3. **Virtual wheel** — the `Windows.Xbox.Input.Controller` statics are wrapped: `GetControllers` returns
   Azimuth's controllers plus one virtual controller. That controller refuses `IGamepad`, answers the
   wheel interface and returns readings built from the DirectInput wheel. Unknown methods return `S_OK`
   and are logged.
4. **Force feedback** — an interpreter (`dll/src/ffb_vm.c`) runs the game's programs at a fixed rate
   (500 Hz by default). The resulting `Af` drives a DirectInput constant-force effect.
5. **Shifter** — gear buttons (from the wheel or a separate shifter device) become slot 32: the first
   engaged gear, `255` for reverse, `0` when none. Slot 30 reports a shifter once any gear is assigned,
   and slot 31 the highest assigned gear. Sequential shift up/down buttons are added to the right/left
   paddles.
6. **DirectInput** — the wheel is read with a shared, background access. Forces go through a second,
   **exclusive foreground** access tied to the game window: some wheels (the Moza R9 among them) silently
   ignore forces from a background application, and stop the effect without any error when another window
   takes the focus. fh2wheel releases the wheel when the game loses focus, takes it back when it regains
   it, and restarts the effect every 200 ms in case the wheel software reset it.

## Open questions

- The meaning of the instructions was inferred from the programs and is covered by
  `tests/unit/test_ffb_vm.c`; it matches what is felt in game.
- Program 3 multiplies the velocity by 100, which hints that Xbox wheels ran these programs around
  100 Hz. The best rate on PC is still being tuned.
- Several wheel methods are still unknown; the log lists those the game calls.
