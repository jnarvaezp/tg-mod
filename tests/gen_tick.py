#!/usr/bin/env python3
import math, struct, sys

rate = 44100
bph = 18000
period = 7200 / bph          # s por beat (0.4 s)
dur = 3.0
freq = 3000.0                # frecuencia de resonancia del tick

# Cada beat contiene dos pulsos: el "tic" (amplitud 1.0) en la fase 0 y un
# "tock" mas debil (amplitud 0.5) a mitad de periodo. La estructura tic+tock
# refleja la firma real de un reloj mecanico y, a diferencia de un tren de
# impulsos limpio, da energia a la autocorrelacion en T/2, lo que evita la
# heuristica "double triggered" de estimate_period() en algo.c (que inflaria
# el periodo a 2x y haria fallar la deteccion automatica con bph=0).
def tick_wave(t, t0, amp):
    dt = t - t0
    if dt < 0 or dt > 0.012:
        return 0.0
    return amp * math.sin(2 * math.pi * freq * dt) * math.exp(-dt * 900.0)

n = int(rate * dur)
samples = []
beats = int(period * rate)
for i in range(n):
    t = i / rate
    bi = i // beats
    v = tick_wave(t, bi * period, 1.0) + tick_wave(t, bi * period + period / 2, 0.5)
    samples.append(v)

with open(sys.argv[1], "wb") as f:
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767)))) for s in samples)
    f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
    f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
    f.write(b"data" + struct.pack("<I", len(data)) + data)
