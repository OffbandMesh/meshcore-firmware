# Building Offband firmware: developer setup

Notes for building from source on your own machine. Users who only flash released
images don't need any of this.

## PlatformIO

Install **PlatformIO Core 6.1.19**, the version CI builds with:

```powershell
pip install platformio==6.1.19
```

Every PlatformIO install in this repo's workflows names that version, and
`scripts/check_pio_pinned.py` fails CI if one doesn't. A different version can change
a build with no change here. To move to a new version, change every pin and the
README in one commit.

## The package store

This project keeps its own PlatformIO package store, separate from every other
project on your machine:

```
<core_dir>\offband-<tag>\packages
<core_dir>\offband-<tag>\platforms
```

`<core_dir>` is your PlatformIO core directory, usually `%USERPROFILE%\.platformio`,
or wherever `PLATFORMIO_CORE_DIR` points. `<tag>` is `[offband] pio_store_tag` in
`platformio.ini`, the pioarduino version this tree builds with.

Why: every pioarduino platform installs its precompiled ESP-IDF libraries into one
shared folder. Two pioarduino versions building on one machine replace that folder
under each other, and the next build fails with compile errors that have nothing to
do with the code. A store per version stops that.

What it means for you:

- **The first build after a clone, or after a platform bump, downloads every
  toolchain into the new store.** That takes a while; later builds don't.
- **When you change a pioarduino platform pin, change `pio_store_tag` with it.**
  `scripts/check_pio_store_tag.py` fails CI if they disagree.
- You can delete an old `offband-<tag>` folder once no branch you build uses that tag.

## Building from Git Bash

ESP-IDF's tool installer refuses to run while `MSYSTEM` is set, and Git Bash sets it.
Unsetting it in the shell doesn't help: Git Bash puts it back for every Windows
program it starts. It matters on the first build into a fresh store, when the
platform installs its tools.

- `scripts/pio-flash` removes it for every PlatformIO command it starts, so it works
  from any shell.
- For a plain `pio run`, use **PowerShell** for the first build into a new store.
  After the tools are installed, builds from Git Bash work.

## Image size

Every ESP32 build checks its real `firmware.bin` against its app slot
(`scripts/size_gate.py`). PlatformIO's own size line leaves out the exception tables
(`.eh_frame`) that the image carries, so it can say an image fits when it doesn't.

Each build prints one line:

```
size-gate: HIGH  image 1,277,072 bytes of a 1,310,720-byte app slot: 97.4%, 33,648 bytes left (report line 90%)
```

`OK` is under the report line, `HIGH` is above it, and `FAIL` stops the build. The
lines are in `scripts/size_budget.json`.

## Measuring growth across a platform bump

Every platform bump runs the size matrix and attaches its grid to the PR:

```powershell
python scripts\size_matrix.py build --tree <worktree on today's platform> --out old
python scripts\size_matrix.py build --tree <worktree on the new platform> --out new
python scripts\size_matrix.py grid --old old --new new
```

With no env names, `build` runs one board per chip in every shipped role. It copies
each env's linker map into the output folder, so the measurement survives a later
rebuild. To explain a change:

- `diff` shows bytes per library between two maps.
- `pulled` shows which object pulled a library into the link.
- `spiffs` shows how many contacts fit a SPIFFS partition of a given size.

[`SIZE-ANALYSIS.md`](initiatives/standalone/2198-esp32-framework/SIZE-ANALYSIS.md) is
the worked example.
