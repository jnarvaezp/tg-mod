# tg — A program for timing mechanical watches

[![Build Status](https://travis-ci.org/vacaboja/tg.svg?branch=master)](https://travis-ci.org/vacaboja/tg)

**Tg** is a timegrapher for mechanical watches: it listens to the noise of a
watch mechanism with a microphone and produces real-time readings of the
**rate** (accuracy), **beat error**, **amplitude** and **beat rate (BPH)**,
with a paperstrip timegraph, waveforms, a live spectrum, statistics and a
per-position report.

## Credits and license

The program is distributed under the GNU GPL license version 2 (see
[LICENSE](LICENSE)). The full source code and its copyright belong to the
respective contributors.

- **Original project:** [vacaboja/tg](https://github.com/vacaboja/tg), by
  **Marcello Mamino** and contributors. This fork keeps all the original
  code and credits intact.
- **Fork maintained by:** Jonathan Narvaez (jnarvaezp) —
  [github.com/jnarvaezp/tg-mod](https://github.com/jnarvaezp/tg-mod).

Some original information can be found in this
[thread at WUS](http://forums.watchuseek.com/f6/open-source-timing-software-2542874.html),
in particular the calibration procedure is described at
[this post](http://forums.watchuseek.com/f6/open-source-timing-software-2542874-post29970370.html).

## Documentation

- **docs/USAGE.md** — full guide (Spanish): theory of the measured values,
  how to interpret the results, and how to set up the microphone.
- **docs/USAGE.en.md** — same guide in English.
- **README.es.md** — this readme in Spanish.
- **docs/ROADMAP.md** — development roadmap of the fork.
- **docs/tg-timer.1** — man page.

## Features

### Original

- Real-time measurements: rate (s/d), beat error (ms), amplitude (deg), BPH.
- Paperstrip timegraph, tic/toc and period waveforms, automatic calibration
  (~15 min), snapshots in tabs, `.tgj` save/load, INI configuration.

### Added in this fork

- **Recording and offline analysis**: record the mic to a WAV
  (*Record to file...*) and analyze existing WAVs without live audio
  (*Open recording...*); headless `tg-timer-dbg analyze file.wav`.
- **Session log**: *Save session log* writes the computation cycles to
  `~/tg-logs/tg-session-<timestamp>.{json,csv,raw}`; `tg-timer debug` /
  `debug full` enable verbose console output in any build.
- **Signal quality**: gain control 0.1–100 (attenuation and amplification),
  live level meter with **CLIP** indicator, robust input device selection
  (ignores the ALSA `(hw:X,Y)` suffix, refresh button, visible fallback
  warning).
- **Live statistics**: rate-trend chart (last ~5 minutes) with mean, standard
  deviation, min and max in s/d.
- **Positions and report**: "pos" selector to tag measurements (dial up/down,
  crown up/down/left/right); *Export report...* writes a per-position summary
  to `~/tg-logs/tg-report-<timestamp>.{csv,pdf}`.
- **Watch database**: watchmaker's logbook — persistent watch registry and
  session history in SQLite (`~/tg-data/tg.db`). The left panel lets you
  create watches, record measurement sessions (tagged with position and
  configuration) and browse the per-watch history.
- **Testing suite**: `make check` (WAV module, session log, DSP regression
  with synthetic clips, stats and report modules).

## Quick start

1. Run `tg-timer`.
2. Select your input device in the **mic** selector (use the ↻ button to
   refresh the list after plugging a USB microphone).
3. Place the watch on the timegrapher microphone (the case or crown against
   the sensor cavity) and make sure the watch is wound.
4. Adjust **gain** so the level bar shows a peak around 0.6–0.9 without the
   **CLIP** indicator appearing.
5. Select the **BPH** (or "guess"), the **lift angle** and the position
   (**pos** selector) for the measurement.
6. Read the results: rate (s/d), beat error (ms), amplitude (deg). See
   docs/USAGE.md for how to interpret them.

## Command line

| Command | Effect |
|---|---|
| `tg-timer` | GUI |
| `tg-timer debug` | GUI + per-cycle console summary |
| `tg-timer debug full` | GUI + full DSP diagnostics |
| `tg-timer -h` / `--help` | Usage |
| `tg-timer-dbg analyze <file.wav>` | Headless analysis (debug build) |
| `tg-timer-dbg test` | 3 s GUI smoke test (debug build) |

## Install instructions

Tg is known to work under Microsoft Windows, OS X, and Linux. Moreover it
should be possible to compile the source code under most modern UNIX-like
systems. See the sub-sections below for the details.

### Windows

Binaries can be found at https://tg.ciovil.li

### Macintosh

A formula for the Homebrew package manager has been prepared by GitHub
user [dmnc](https://github.com/dmnc). To use it, you need to install
Homebrew first (instructions on http://brew.sh).

Then run the following command to check everything is set up correctly
and follow any instructions it gives you.

	brew doctor

To install tg, run

	brew install dmnc/horology/tg

You can now launch tg by typing

	tg-timer &

### Debian or Debian-based (e.g. Mint, Ubuntu)

Binary .deb packages can be downloaded from https://tg.ciovil.li

## Compiling from sources

The source code of tg can probably be built by any C99 compiler, however
only gcc and clang have been tested. You need the following libraries:
gtk+3, portaudio2, fftw3, sqlite3 (all available as open-source).

Release build:
```sh
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

Debug build:
```sh
make tg-timer-dbg
```

### Compiling on Windows

It is suggested to use the msys2 platform. First install msys2 according
to the instructions at [http://www.msys2.org](http://www.msys2.org). Then
issue the following commands.

```sh
pacman -S mingw-w64-x86_64-gcc make pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-portaudio mingw-w64-x86_64-fftw mingw-w64-x86_64-sqlite3 git autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

### Compiling on Debian

To compile tg on Debian

```sh
sudo apt-get install libgtk-3-dev libjack-jackd2-dev portaudio19-dev libfftw3-dev libsqlite3-dev git autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

The package libjack-jackd2-dev is not necessary, it only works around a
known bug (https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=718221).

### Compiling on Fedora

To compile tg on Fedora

```sh
sudo dnf install fftw-devel portaudio-devel gtk3-devel sqlite-devel autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

## Testing

- `make check` runs the unit tests: WAV module, session log, DSP regression
  (synthetic clips at 12000–36000 BPH, with/without noise, controlled beat
  error), stats and report modules. Drop real recordings into
  `tests/fixtures/*.wav` to include them in the DSP regression suite (they
  must at least detect a signal).