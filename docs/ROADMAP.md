# Roadmap de mejoras de tg

Cronocomparador (timegrapher) de código abierto para relojes mecánicos.
Este documento define el roadmap de mejoras de valor, el orden de dependencia
y la rama en la que se desarrollará cada fase.

Premisas de trabajo: ver `docs/DEVELOPMENT.md`.

## Estado actual

- **Rama:** `feature/input-device-selector` (trabajo pendiente de integrar en `master`).
- Cambios sin commitear: selección de dispositivo de entrada, ganancia, cutoff
  del filtro, espectro en vivo y marcadores de 3 fases.
- Características existentes: rate (s/d), beat error (ms), amplitud (grados),
  BPH, calibración automática (~15 min), paperstrip, formas de onda tic/toc y
  periodo, snapshots en pestañas, guardado/carga `.tgj`, persistencia de
  configuración (INI).

## Fase 0 — Integración del trabajo pendiente

**Rama:** `feature/input-device-selector` → `master`

1. Commit del código pendiente + `build.sh`.
2. Merge a `master` y push.
3. Eliminar la rama feature tras el merge.

## Fase 1 — Grabación + análisis offline (completada)

**Estado:** completada. Añade `src/wav.c`, `src/offline.c` y grabación a WAV.
Uso: menú Command → «Open recording…» para analizar un WAV sin micrófono;
«Record to file…» para grabar; CLI `tg-timer-dbg analyze <archivo.wav>`.

**Rama:** `feature/recording-offline`

- Grabación de audio (WAV) desde el micrófono.
- Apertura de WAV existentes para análisis offline.
- Fuente de audio abstracta (micrófono o archivo) alimentando la misma
  pipeline DSP (`audio.c` / `algo.c`).
- Habilita el resto de fases (clips de referencia para tests, análisis sin
  silencio, compartir muestras).

## Fase 1.5 — Session log + verbose (completada)

**Rama:** `feature/session-log` — **Estado:** completada.

- Menú Command → *Save session log*: guarda en `~/tg-logs/` tres archivos por
  sesión (`tg-session-<timestamp>.{json,csv,raw}`) con los ciclos de cómputo
  (timestamp, signal, bph, rate, be, amp, period, sigma) y el texto de debug
  crudo. Funciona en builds release y debug.
- Argumento `tg-timer debug`: salida verbosa de consola (DSP) en cualquier
  build.

## Fase 2 — Tests de regresión DSP

**Rama:** `feature/dsp-regression-tests`

- Suite de tests con clips WAV de referencia (reloj a varias BPH).
- Validación de `period` / `rate` / `be` / `amp` de `algo.c`.
- Estructura de tests en C (assert + make target) o script comparador.

## Fase 3 — Calidad de señal / diagnóstico

**Rama:** `feature/signal-quality`

- SNR del tick, nivel de entrada, detección de recorte y ruido de fondo.
- Medidor visual + sugerencia de ganancia / cutoff.

## Fase 4 — Estadísticas y tendencia

**Rama:** `feature/stats-trend`

- Media / desviación / min / max del rate en vivo.
- Gráfico de tendencia temporal junto al paperstrip.

## Fase 5 — Multi-posición + informe

**Rama:** `feature/positions-report`

- Etiquetado de posición por medición (dial up/down, crown left/right, ...).
- Resumen por posición (media / ΔSD).
- Exportación de informe (CSV/PDF).

## Fase 6 — i18n + pulido de UX

**Rama:** `feature/i18n-ux`

- gettext (traducciones).
- Tooltips, atajos de teclado, accesibilidad en los dibujos Cairo.

## Orden de ejecución

Las fases se ejecutan en orden (1 → 6). Cada fase:

1. Se crea su rama `feature/<nombre>` desde `master`.
2. Se escribe su plan detallado en `docs/superpowers/plans/`.
3. Se implementa con TDD y commits pequeños.
4. Se verifica (`make test`).
5. Se integra en `master`, push y eliminación de la rama.
