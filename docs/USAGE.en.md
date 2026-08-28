# tg usage guide

Timegrapher for mechanical watches. This guide explains what the software
does, the theory of the values it measures, and how to interpret the results.

## 1. How it works

1. **Capture**: the microphone (or a WAV file in offline mode) delivers
   44100 Hz audio samples.
2. **Filtering**: a band-pass filter (high-pass + low-pass envelope at
   `cutoff`) isolates the band where the tick energy concentrates and
   removes low-frequency noise.
3. **Period detection**: the autocorrelation of the signal finds the
   distance between impacts (tic-tock), i.e. the **period** of the
   escapement.
4. **Parameters**: from the period and the folded waveform, the software
   computes rate, beat error, amplitude and BPH.
5. **Display**: paperstrip, waveforms, spectrum, statistics and the numeric
   readout.

## 2. The measured values (theory and interpretation)

### BPH (beats per hour)

The **number of half-oscillations per hour** of the balance: 18000, 21600,
28800, 36000 are the most common. The app measures the full oscillation
period (tic+tock); `BPH = 7200 / period(seconds)`.

- With **"guess"** the app tries to infer the BPH from the measurement
  (compared against presets).
- If you know the watch's BPH, select it: detection is more robust.
- **Note**: with a known BPH the guard accepts up to 20% over the nominal
  period, so watches from ~8100 BPH (pocket watches) can be measured. When
  guessing, the effective range starts at ~12000 BPH.

### Rate (s/d) — the accuracy

How many **seconds per day the watch gains (positive) or loses (negative)**:

```
rate = (measured_period / nominal_period - 1) * 86400  [s/d]
```

- **0 s/d** = perfect. **±10 s/d** = an ordinary watch.
- A well-regulated watch (COSC standard): between **−4 and +6 s/d**.
- **Real example**: +177 s/d means it gains ~2 minutes per day — a very fast
  watch, typical of a damaged balance wheel, magnetization, or a damaged
  hairspring.
- **Reading it in practice**: the rate must be *stable* (see σ) and similar
  between positions (see report). A rate that changes a lot with position
  points to regulation problems or damaged pivots.

### Sigma (σ) — the stability

Standard deviation of the rate between measurement cycles. **The lower, the
more stable** the measurement:

- σ < 2 s/d: stable and reliable measurement.
- Large σ (10+ s/d): weak signal, noise, or the watch is not running
  regularly.

### Beat error (ms) — the tic-tock symmetry

Measures the asymmetry between the tic→tock and tock→tic intervals:

```
be = |period/2 − (toc − tic)|
```

- **Ideal: < 1 ms**. Values of 3–6 ms are common in unregulated watches and
  are corrected by moving the **regulator** (or, with a movable stud, the
  hairspring).
- A high beat error makes the measured rate depend on the microphone and
  position; regulate it first.

### Amplitude (degrees) — escapement health

The **oscillation angle of the balance wheel**. Estimated from the width of
the tic/tock pulses in the waveform:

```
amp = 0.5 / sin(π · pulse_width / period)
```

Typical healthy ranges by BPH:

| BPH | Healthy amplitude |
|---|---|
| 18000 | 180° – 250° |
| 21600 | 220° – 300° |
| 28800 | 250° – 310° |

- **Low amplitude** (< 150–180°): worn pivots/jewels, old oil, or the watch
  not fully wound.
- **High amplitude** (> 330°): over-banking (knocking).
- The app shows the estimated amplitude **even when outside the physical
  range** (135°–360°), marked with an asterisk (`100*`) — it is diagnostic
  information: low amplitude indicates wear or lack of winding. It only
  shows `---` when it cannot measure the pulses at all.

### Lift angle (degrees)

The **lift angle**: the arc during which the escapement receives the impulse
from the balance. It is a caliber parameter (typically 44°–56°; default 52°).
It is used to compute the amplitude; if you do not know the exact value for
your caliber, the amplitude will be proportionally off, but rate and beat
error are unaffected.

### Calibration (s/d) — the real microphone sample rate

Some USB sound cards do not produce exactly 44100 Hz. Calibration measures
the **real sample-rate deviation** (~15 min) and compensates for it in all
rate calculations. The value shows in the `cal` control (in tenths of s/d).

- If `cal = 0` and your card is off, you will see a false rate (e.g. +50 s/d
  on a healthy watch). That deviation adds to the real measurement.
- **Flow**: watch running on the mic → Command menu → *Calibrate* → wait for
  progress → the value is applied automatically.

### Signal — detection

The app indicates whether it is getting a valid measurement:

- **Signal = 1**: stable detection (the displayed value is reliable).
- **Signal = 0**: no periodic tick detected — check: is the watch wound? Is
  it seated on the microphone? Is the signal clipping (CLIP) or too low?

## 3. Practical configuration

### Gain

Range **0.1–100**: 1.0 = unchanged, < 1 attenuates, > 1 amplifies.

- **Rule**: adjust so the level bar shows a **peak ≈ 0.6–0.9** and **no
  CLIP**.
- Peak > 0.98 (CLIP): the signal saturates and distorts — lower the gain or
  the system input level.
- Peak < 0.2: the signal may be too weak for a stable measurement.
- With a timegrapher microphone (e.g. TGBC, with built-in amplifier) a gain
  of **0.3–0.7** usually suffices.

### Cutoff (filter cutoff frequency)

The band-pass filter is centered at this frequency (default **3000 Hz**).

- The **high-pass** removes low-frequency noise (hum, ambiance).
- The **low-pass** (after rectification) smooths the tick envelope.
- The ideal value is **where your microphone resonates** (piezo mics usually
  resonate at 1–5 kHz). To find it, measure the same watch with cutoff 2000 /
  3000 / 4000 and keep the one that gives the lowest `sigma`.
- Changing the cutoff restarts acquisition (the strip clears).

### Input device

- Select the microphone in the **mic** selector; if you replugged it and it
  does not appear, use the **↻** button.
- If the saved device is not found at startup, the app warns and uses the
  system default.

### Light algorithm

Halves the sample rate (22050 Hz) to lighten the computation. Useful on slow
machines; not needed for normal watches.

## 4. The panel

- **Big readout**: rate, beat error, amplitude, guessed BPH, and a signal
  indicator (watch icon color).
- **Paperstrip**: each tick lands as a dot; the slope of the line is the
  rate. Buttons `<`, `>`, `Center`, `Clear`.
- **Tic/toc waveforms**: the folded pulse of each beat (with 3-phase
  unlock/impulse/drop markers when detected).
- **Period waveform**: the full oscillation with the tic/toc windows shaded.
- **Spectrum**: live frequency analysis of the input.
- **Trend**: the rate of the last ~5 minutes with mean, σ, min and max.
- **Position (pos)**: tags the measurements with the watch position.

## 5. Recording and offline analysis

- **Record**: Command → *Record to file...* saves the mic audio to a WAV;
  *Stop recording* finalizes it.
- **Open recording**: Command → *Open recording...* analyzes a WAV without a
  microphone (the strip plays at real time).
- **Headless**: `tg-timer-dbg analyze file.wav` prints
  signal/bph/rate/be/amp and exits (for automation and testing).
- **Check a recording's level**:
  `python3 tests/check_level.py file.wav` (clip%, peak, RMS, envelope
  autocorrelation — a clear peak ~0.3–0.6 s indicates a periodic tick).

## 6. Session log (debugging)

Command → *Save session log* saves three files per session in `~/tg-logs/`:

- **`.json`** — one object per computation cycle (wall_ms, audio, signal,
  bph, rate, be, amp, period, sigma, calibrate, cal_state).
- **`.csv`** — the same data as a table (for Excel/scripts).
- **`.raw`** — the full debug text with timestamps.

Console: `tg-timer debug` (per-cycle summary) or `tg-timer debug full` (all
detection details).

## 7. Positions, report and the watchmaker's logbook

1. Measure 30–60 s in one position (e.g. dial up) with the **pos** selector
   set to that position.
2. Repeat for the other positions.
3. Command → *Export report...* → writes `~/tg-logs/tg-report-<timestamp>
   .{csv,pdf}` with the per-position summary: n, mean, σ, min, max of the
   rate + mean of be and amp.

**Interpreting the report**: the difference between the best and worst
position (Δ between max and min mean) indicates the state of the
regulation. A healthy watch usually varies < 15–20 s/d between positions;
large variations point to pivots, hairspring or balance problems.

### The watchmaker's logbook (left panel)

- **Create a watch**: *New watch...* button (name required, brand and model
  optional).
- **Record a session**: select the watch, choose the position with the
  **pos** selector, press *Start session*, measure 30–60 s, then press
  *Finish & save*. The session is recorded with its position, note,
  statistical summary (n, mean, σ, min, max) and the configuration used
  (bph, lift angle, cal, gain, cutoff).
- **History**: the panel table shows all sessions of the selected watch
  with their date — track the evolution of the regulation (before/after a
  repair, for example). Select a row and use *Delete session* to remove it
  from the history.
- Statistics show the rate with an explicit sign (+/− in s/d); sessions
  captured without a valid signal show `---`.
- The database lives in `~/tg-data/tg.db` (SQLite).

## 8. Troubleshooting

| Symptom | Likely cause | Solution |
|---|---|---|
| Spectrum moves but no data | Watch not wound, badly seated, weak/clipping signal | Wind it, seat the watch on the cavity, adjust gain (no CLIP), reduce noise |
| "Saved input device ... not found; falling back to default" | Mic was replugged (`hw:X,Y` changed) | Re-select the device in **mic** (↻ button) |
| CLIP on | Signal saturates | Lower the gain or the system input level |
| Odd rate (~+50 s/d) on a healthy watch | Real sample rate of the card is off | Run *Calibrate* (~15 min) |
| Slow BPH (12000/14400) not detected with "guess" | When guessing, the effective range starts at ~12000 BPH | Select the known BPH (with it, watches from 8100 BPH measure) |
| Report export empty | Cycles without a tagged position | Select "pos", or use the "none"/total row (fixed) |