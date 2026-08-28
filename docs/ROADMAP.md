# Roadmap de mejoras de tg

Cronocomparador (timegrapher) de código abierto para relojes mecánicos.
Este documento define el roadmap de mejoras de valor, el orden de dependencia
y la rama en la que se desarrollará cada fase.

Premisas de trabajo: ver `docs/DEVELOPMENT.md`.

## Estado actual

- **Master** en `37cceb6` (pusheado a `origin` = fork propio). Sin ramas
  feature pendientes de integrar; todas las completadas se eliminaron.
- Fases completadas: 0 (integración del trabajo pendiente), 1 (grabación +
  análisis offline), 1.5 (session log + verbose), 2 (tests de regresión DSP),
  3-base (calidad de señal), 4 (estadísticas y tendencia), 5 (multi-posición
  + informe).
- Características existentes: rate (s/d), beat error (ms), amplitud (grados),
  BPH, calibración automática (~15 min), paperstrip, formas de onda tic/toc y
  periodo, espectro en vivo, snapshots en pestañas, guardado/carga `.tgj`,
  persistencia de configuración (INI), grabación WAV, análisis offline
  (`--analyze`), session log (JSON/CSV/raw), control de ganancia con medidor
  de nivel y selección robusta de dispositivo, estadísticas en vivo con
  gráfico de tendencia, posiciones e informe (CSV/PDF), suite de pruebas
  (`make check`).
- Documentación: README bilingüe (README.md / README.es.md), guía completa
  (docs/USAGE.md / docs/USAGE.en.md), spec del registro de relojes
  (docs/superpowers/specs/2026-08-24-watch-db-design.md).

## Estabilización — crash de reinicio y dispositivos (hecho)

**Rama:** `feature/stability-fix` — **Estado:** completada.

- Segfault al reabrir audio con el dispositivo guardado ausente (re-entrancia
del timeout kick_computer dentro del diálogo de fallback): corregido.
- Matching de dispositivo por nombre de tarjeta (inmune a cambios de nombre
PCM y de índice hw al reconectar el USB).
- Errores transitorios de ALSA reportados; guards NULL en device/stream info;
clamps y inicializaciones menores.

## Fix pendiente — Informe vacío (hecho)

**Rama:** `feature/report-fix` — **Estado:** completada.

El informe exportado excluía los ciclos sin posición etiquetada. Ahora
incluye la fila "none" (ciclos sin etiquetar) y la fila "Total", y escribe
"no data" si no hay mediciones.

## Fase 5.5a — Registro de relojes (watch-db) (completada)

**Rama:** `feature/watch-db` — **Estado:** completada.

- Base SQLite en `~/tg-data/tg.db`: tablas `watches` y `sessions` (FK CASCADE,
  snapshot de configuración por sesión: bph, lift angle, cal, gain, cutoff).
- CRUD completo de relojes y sesiones (`watchdb.c`) + captura de sesión
  (`watchdb_capture_session`) + export JSON por reloj (`json.c`).
- Campos del reloj: nombre, marca, modelo/calibre, serial, año, notas +
  defaults de análisis (bph, lift angle).
- Pendiente (5.5b): panel izquierdo UI, ciclo de sesión y gráfico de evolución.

## Fase 5.5b — Panel de sesiones (watch-panel)

**Rama:** `feature/watch-panel`

- Panel izquierdo: lista de relojes (crear/renombrar/eliminar), historial de
  sesiones por reloj (fecha, posición, n, media, σ, be, amp) consumiendo
  `watchdb.c`.
- Ciclo de sesión manual: Iniciar sesión / Finalizar y guardar (con la
  posición y configuración actuales).
- Gráfico de evolución del rate por sesión y defaults de análisis por reloj.

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
  build; `tg-timer debug full` añade el detalle por detección (`max/med/cnt`).
  Niveles: 0 = silencioso, 1 = resumen por ciclo, 2 = detalle.

## Fase 2 — Tests de regresión DSP (completada)

**Rama:** `feature/dsp-regression-tests` — **Estado:** completada.

- `tests/test_dsp.c` (make check): clips sintéticos a 12000–36000 BPH con y sin
  ruido, beat error controlado, y fixtures reales opcionales en
  `tests/fixtures/*.wav` (deben detectar señal). Valida signal/bph/rate/be/amp
  vía `analyze_audio_file()`.
- Nota: 12000/14400 BPH quedan fijados como `signal=0` (el guard de
  `process()`, algo.c:978, rechaza periodos ≥ 0.5 s a 44.1 kHz — limitación
  conocida, pendiente de futuro ajuste).

## Fase 3 — Calidad de señal / diagnóstico (parcialmente completada)

**Rama:** `feature/signal-quality` — **Estado:** implementada la base.

- Ganancia 0.1–100 (atenuación) y medidor de nivel en vivo con alerta CLIP.
- Emparejamiento robusto de dispositivo (ignora `(hw:X,Y)`), aviso de fallback
  y refresco de la lista.
- Amplitud visible aunque esté fuera del rango físico (135–360°), con
  marcador `*` (baja = desgaste/poca cuerda; alta = knocking).
- Pendiente: SNR/indicadores más finos, sugerencia automática de ganancia,
  control del volumen de captura ALSA.

## Fase 4 — Estadísticas y tendencia (completada)

**Rama:** `feature/stats-trend` — **Estado:** completada.

- Módulo `stats` (anillo ~60 min): media/σ/min/max del rate con ventana.
- Gráfico de tendencia del rate en el panel (bajo el espectro) con cuadrícula
  centrada en 0, línea del rate y resumen numérico en vivo.
- El botón Clear resetea también las estadísticas.

## Fase 5 — Multi-posición + informe (completada)

**Rama:** `feature/positions-report` — **Estado:** completada.

- Selector de posición en la UI (none / dial up / dial down / crown up /
  crown down / crown left / crown right) que etiqueta los ciclos en vivo.
- Resumen por posición (n, media/σ/min/max del rate + media de be/amp).
- Menú Command → *Export report...*: escribe en `~/tg-logs/`
  `tg-report-<timestamp>.{csv,pdf}` (PDF generado con Cairo).

## Fase 7a — Port de fixes algorítmicos de xyzzy42 (completada)

**Rama:** `feature/xyzzy42-algo` — **Estado:** completada.

Port de [xyzzy42/tg](https://github.com/xyzzy42/tg) por **Trent Piepho** (GPL-2):

- **BPH bajos soportados hasta 8100**: el guard fijo de 0.5 s (que hacía
  imposibles 12000/14400) se reemplaza por un límite dinámico (1.2× el
  periodo nominal con BPH conocido). `test_dsp` actualizado.
- Sigma de 1 muestra = 0 (antes = periodo, siempre rechazada).
- `conjf` para float complex y alocación FFTW uniforme.
- Amplitud estimada visible aunque esté fuera del rango físico (135–360°),
  marcada con `*` y acotada a 720°.
- Pendiente (7b): paperstrip v2 (zoom, timestamps, colores, overlay amplitud);
  7c: tppm + procesamiento por paso.

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
