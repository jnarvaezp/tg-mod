# A program for timing mechanical watches [![Build Status](https://travis-ci.org/vacaboja/tg.svg?branch=master)](https://travis-ci.org/vacaboja/tg)

The program tg is distributed under the GNU GPL license version 2. The full
source code of tg is available at
[https://github.com/vacaboja/tg](https://github.com/vacaboja/tg) and its
copyright belongs to the respective contributors.

Tg is in development, and there is still no manual. Some info can be found
in this
[thread at WUS](http://forums.watchuseek.com/f6/open-source-timing-software-2542874.html),
in particular the calibration procedure is described at
[this post](http://forums.watchuseek.com/f6/open-source-timing-software-2542874-post29970370.html).

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

## Recording and offline analysis

- **Record**: Command menu → *Record to file...* saves the mic input to a
  WAV file; *Stop recording* finalizes it.
- **Analyze offline**: Command menu → *Open recording...* loads a WAV and
  runs the same analysis pipeline without needing live audio. While a
  recording is open, the timegrapher replays it at real time.
- **Headless**: `tg-timer-dbg analyze file.wav` (debug build) prints
  rate / beat error / amplitude / BPH and exits.

## Debugging

- **Verbose console**: run `tg-timer debug` to print the DSP diagnostics to
  the terminal in any build (release included).
- **Session log**: Command menu → *Save session log* writes the computation
  cycles to `~/tg-logs/tg-session-<timestamp>.{json,csv,raw}` (structured
  JSON/CSV + raw debug text) for post-mortem analysis.

## Compiling from sources

The source code of tg can probably be built by any C99 compiler, however
only gcc and clang have been tested. You need the following libraries:
gtk+3, portaudio2, fftw3 (all available as open-source).

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
pacman -S mingw-w64-x86_64-gcc make pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-portaudio mingw-w64-x86_64-fftw git autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

### Compiling on Debian

To compile tg on Debian

```sh
sudo apt-get install libgtk-3-dev libjack-jackd2-dev portaudio19-dev libfftw3-dev git autoconf automake libtool
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
sudo dnf install fftw-devel portaudio-devel gtk3-devel autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```
