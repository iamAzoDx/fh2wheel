# fh2wheel

Play **Forza Horizon 2** (Xbox One, version 1.0.0.64) in the **Azimuth** emulator with a
**PC racing wheel and real force feedback**.

Forza Horizon 2 supports Xbox One racing wheels out of the box, force feedback included, but Azimuth
only emulates the gamepad. fh2wheel shows the game a virtual Xbox One wheel, fed by your DirectInput
wheel, and runs the force feedback programs the game sends to it.

- Real wheel mode in game: steering, pedals, paddles, buttons.
- Force feedback computed by the game itself: road feel, grip, impacts, centering spring in menus.
- Works alongside fh2core and fh2online.
- Made for any DirectInput wheel, with a setup tool to map axes and buttons.

> **Tested hardware:** Moza R9 base with the ESX wheel only. Other wheels should work but have not been
> tried yet. If you test one, please open an issue with your wheel model and `data\logs\fh2wheel.log`.

fh2wheel is a fan project. It is not affiliated with or endorsed by Microsoft, Xbox Game Studios,
Playground Games, Turn 10 Studios, Almost There Games or any wheel maker. You need your own legally
obtained copy of the game.

---

## Installation tutorial

### What you need

- **Azimuth 0.9.9** with **Forza Horizon 2 1.0.0.64** already installed and running correctly. fh2wheel
  does not provide the game or the emulator.
- Nothing else. fh2core / the FH2 DLC Launcher (Storm Island) is **not** required: fh2wheel works when
  the game is started with `azimuth.exe` directly, and also alongside fh2core. It has been tested with the
  FH2 ExtraOptions mod installed; it is designed to work without it too, but that has not been tested
  yet.
- A **racing wheel** that works in other PC games, with its driver and software installed (Moza Pit
  House, Logitech G HUB, Thrustmaster drivers…).
- The latest `fh2wheel-vX.zip` from the [Releases](../../releases) page.

### Step 1 — Copy the files

1. Close the game.
2. Open the folder that contains `azimuth.exe`.
3. Extract these files from the zip **into that same folder**:
   - `D3DCOMPILER_47.dll`
   - `fh2wheel.dll`
   - `fh2wheel-config.exe`
   - `fh2wheel-probe.exe` (optional, for troubleshooting)

The folder should now look like this:

```
azimuth.exe
D3DCOMPILER_47.dll    <- new
fh2wheel.dll          <- new
fh2wheel-config.exe   <- new
data\
...
```

`D3DCOMPILER_47.dll` is a small loader: Azimuth loads it at start-up, it forwards every call to the real
Windows file and loads `fh2wheel.dll`. No game, emulator or Windows file is modified.

### Step 2 — Set up your wheel

Plug in your wheel, keep the game closed, and run **`fh2wheel-config.exe`**.

1. **Wheel**: pick your wheel in the list. If your pedals are a separate USB device, pick them under
   **Pedals**. Otherwise leave “None”.
2. **Steering**: click **Detect**, then turn the wheel to the **right**. Check that the bar moves to the
   right when you turn right. If not, tick **Invert**.
3. **Throttle, Brake, Clutch, Handbrake**: click **Detect**, then press the pedal all the way. Each bar
   must fill up when you press its pedal.
4. **Buttons**: for each Xbox control (A, B, X, Y, View, Menu, D-pad, paddles), click **Assign** and press
   the wheel button you want. A ● appears next to a control while its button is pressed.
5. **Force feedback**: click **Test: push right**. The wheel must turn to the right. If it turns to the
   left, tick **Invert force** and test again.
6. Click **Save**. The settings go to `data\fh2wheel.ini`.

### Step 3 — Play

Start the game the usual way (`azimuth.exe`, or `FH2 DLC Launcher.exe` if you use the Storm Island
mod). Turn off your Xbox controller first, otherwise the game may use it instead of the wheel.

**Keep the game window focused.** Windows only lets the focused application drive the wheel motor. When
you come back to the game after using another window, force feedback resumes within a second.

### Uninstall

Delete `D3DCOMPILER_47.dll`, `fh2wheel.dll`, `fh2wheel-config.exe`, `fh2wheel-probe.exe` and, if you like,
`data\fh2wheel.ini` and `data\logs\fh2wheel.log`.

---

## Tuning

| Setting | Effect |
|---|---|
| **Rotation °** | Rotation the game assumes for your wheel. Set your wheel software to the same angle (900 by default). |
| **Gain %** | Overall force strength. The game sometimes asks for more than 100 %; if big hits feel flat, lower it. |
| **Rate Hz** | How often the force is computed (default 500). Lower values make the game's damping heavier. |
| **Invert force** | Use it if the wheel pushes away from the center instead of towards it. |

Effects set in your wheel software (damping, friction, inertia…) add to the game's forces. Lower them if
the wheel feels heavy or slow to return to the center.

## Troubleshooting

| Problem | What to check |
|---|---|
| The game does not react to the wheel | The wheel must be plugged in **before** starting the game. Check that `data\logs\fh2wheel.log` exists and lists your wheel. |
| No force feedback | Run `fh2wheel-config.exe` with the game closed and use the force tests. Keep the game window focused while playing. |
| Wrong buttons or inverted pedals | Run `fh2wheel-config.exe` again and use **Detect** / **Assign**. |
| Something else | Tick **Detailed log (trace)**, save, play for a minute, then open an issue with `data\logs\fh2wheel.log`. |

`fh2wheel-probe.exe` (console) helps with diagnosis:

```
fh2wheel-probe.exe                          list DirectInput devices
fh2wheel-probe.exe 346E:0002 30             show live axes and buttons for 30 s (use your VID:PID)
fh2wheel-probe.exe --foreground 346E:0002   send test forces
```

---

## Building from source

Requirements: Linux or WSL with `mingw-w64`, `gcc`, `make` and `python3`.

```
make check      # build everything and run the tests
make release    # build the release zip in build/
```

How it works: [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md). Contributions are welcome, see
[CONTRIBUTING.md](CONTRIBUTING.md).

## License and credits

fh2wheel is free software under the [MIT license](LICENSE): anyone can use, copy, modify and share it.
See [CREDITS.md](CREDITS.md) for everything this project builds on, and
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for third-party licenses.
