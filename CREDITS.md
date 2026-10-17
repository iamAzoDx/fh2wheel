# Credits

## Project

- **AzoDx** — creator, testing on real hardware, design decisions.
- **Claude** (Anthropic), through **Claude Code** — AI pair programmer: reverse engineering of the game's
  wheel support, code, tests and documentation, under AzoDx's direction. Model used: Claude Opus 5.5.
  No Claude Code skills or plugins were used for this project; the work relied on Claude Code's standard
  tools (file editing, shell) and the open-source tools listed below.

## The game and the emulator

fh2wheel only makes sense thanks to these works. It contains none of their code or data.

- **Forza Horizon 2** — developed by Playground Games and Turn 10 Studios, published by Microsoft /
  Xbox Game Studios. Forza and Xbox are trademarks of Microsoft.
- **Azimuth** — Xbox One emulator by **Almost There Games**. fh2wheel plugs into the controller list
  Azimuth gives to the game.
- **fh2core / FH2 DLC Launcher** (“FH2 Storm Island for Azimuth”) — the community mod that lets Azimuth
  load the Storm Island DLC, shared in the [AskDriving](https://www.youtube.com/@askdriving) video
  tutorial. Its original author is not named in the package. fh2wheel contains none of its code; it is
  built to run alongside it.
- **fh2online** — AzoDx's project to restore Forza Horizon 2 online play; fh2wheel reuses its loader and
  import-hooking techniques (proxy DLL, IAT chaining).

## Tools used to build and study

- **Ghidra** (NSA, Apache License 2.0) — decompilation of the game's wheel and force feedback code.
- **GNU Binutils** (`objdump`, GPL) — disassembly.
- **mingw-w64** and **GCC** — Windows cross-compiler (the mingw-w64 runtime is linked into the binaries,
  see THIRD-PARTY-NOTICES.md).
- **Python** — build and test tools.
- **Windows Subsystem for Linux** and **PowerShell** (Microsoft) — build and test environment.
- **WheelCheck** — “iRacing Force Feedback Test” by **iRacing.com**, used to check that the wheel accepts
  DirectInput forces and to find out that the forces must come from the foreground window.
- **Microsoft DirectInput documentation** and its **FFConst** force feedback sample — reference for
  creating force effects.

## Hardware

- **Moza Racing** R9 base and ESX wheel, with **Moza Pit House** — the setup fh2wheel was developed and
  tested on.

If you contributed and are missing here, open a pull request adding yourself.
