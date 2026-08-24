#!/usr/bin/env python3
"""Nivel de un WAV: clip%, peak, rms y autocorrelación de la envolvente.
Uso: python3 tests/check_level.py archivo.wav"""
import wave, struct, sys, math

path = sys.argv[1]
w = wave.open(path, 'rb')
sr = w.getframerate()
n = w.getnframes()
data = w.readframes(n)
if w.getsampwidth() == 2:
    samples = struct.unpack('<%dh' % n, data)
else:
    raise SystemExit("solo 16-bit")
w.close()
samples = [s/32768.0 for s in samples]
n = len(samples)

clip = sum(1 for s in samples if abs(s) >= 0.999) / n * 100
peak = max(abs(s) for s in samples)
rms = math.sqrt(sum(s*s for s in samples)/n)
print(f"dur={n/sr:.2f}s clip%={clip:.3f} peak={peak:.3f} rms={rms:.4f}")

ws = max(1, int(sr*0.005))
env = []
for i in range(0, n-ws, ws):
    seg = samples[i:i+ws]
    env.append(math.sqrt(sum(s*s for s in seg)/len(seg)))
m = sum(env)/len(env)
e = [x-m for x in env]
lo = int(sr*0.05/ws); hi = int(sr*1.5/ws)
best = []
for lag in range(lo, min(hi, len(e)//2)):
    acc = sum(e[i]*e[i+lag] for i in range(0, len(e)-lag, 4))
    best.append((acc, lag))
best.sort(reverse=True)
lag0 = sum(x*x for x in e) or 1
for acc, lag in best[:5]:
    print(f"  env autocorr lag={lag*ws/sr:.4f}s norm={acc/lag0:.3f}")