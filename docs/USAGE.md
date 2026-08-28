# Guía de uso de tg

Cronocomparador (timegrapher) para relojes mecánicos. Esta guía explica qué
hace el software, la teoría de los valores que mide y cómo interpretar los
resultados.

## 1. Cómo funciona

1. **Captura**: el micrófono (o un archivo WAV en modo offline) entrega
   muestras de audio a 44100 Hz.
2. **Filtrado**: un filtro pasabanda (high-pass + envolvente low-pass en
   `cutoff`) aísla la banda donde se concentra la energía del tic y elimina
   ruido de baja frecuencia.
3. **Detección del periodo**: la autocorrelación de la señal encuentra la
   distancia entre golpes (tic-tock), es decir el **periodo** del escape.
4. **Parámetros**: con el periodo y la forma de onda plegada se calculan
   rate, beat error, amplitud y BPH.
5. **Presentación**: papelera (paperstrip), formas de onda, espectro,
   estadísticas y los valores numéricos en pantalla.

## 2. Los valores medidos (teoría e interpretación)

### BPH (beats per hour)

El **número de semioscilaciones por hora** del volante: 18000, 21600, 28800,
36000 son los más comunes. La app mide el periodo de la oscilación completa
(tic+tock); `BPH = 7200 / periodo(segundos)`.

- Con **"guess"** la app intenta adivinar el BPH a partir de la medición
  (compara contra los presets).
- Si sabes el BPH del reloj, selecciónalo: la detección es más robusta.
- **Nota**: a 44.1 kHz la app no puede medir periodos ≥ 0.5 s (BPH ≤ 14400).
  Para relojes lentos (pocket watches) la medición no está disponible.

### Rate (s/d) — la precisión

Indica **cuántos segundos por día adelanta (positivo) o atrasa (negativo)**
el reloj respecto al tiempo real:

```
rate = (periodo_medido / periodo_nominal - 1) * 86400  [s/d]
```

- **0 s/d** = perfecto. **±10 s/d** = un reloj común y corriente.
- Un reloj bien regulado (estándar COSC): entre **−4 y +6 s/d**.
- **Ejemplo real**: +177 s/d significa que adelanta ~2 minutos por día — un
  reloj muy rápido, típico de una rueda de balance dañada, imán o
  hairspring en mal estado.
- **Cómo leerlo en la práctica**: el rate debe ser *estable* (ver σ) y
  parecido entre posiciones (ver informe). Un rate que cambia mucho según la
  posición indica problemas de regulación o de asiento de las pivotes.

### Sigma (σ) — la estabilidad

Desviación estándar del rate entre ciclos de medición. **Cuanto menor, más
estable** es la medición:

- σ < 2 s/d: medición estable y fiable.
- σ grande (10+ s/d): señal débil, ruido, o el reloj no está en marcha
  regular.

### Beat error (ms) — la simetría tic-tock

Mide la asimetría entre el intervalo tic→tock y tock→tic dentro de la
oscilación:

```
be = |periodo/2 − (toc − tic)|
```

- **Ideal: < 1 ms**. Valores de 3–6 ms son comunes en relojes sin regular y
  se corrigen moviendo la **raqueta** (regulador) o, en relojes con
  **estud móvil**, ajustando el hairspring.
- Un beat error alto hace que el rate medido dependa del micrófono y de la
  posición; por eso se regula antes que nada.

### Amplitud (grados) — la salud del escape

El **ángulo de oscilación del volante** (amplitud de balanceo). Se estima a
partir del ancho de los pulsos tic/tock en la forma de onda:

```
amp = 0.5 / sin(π · ancho_pulso / periodo)
```

Rangos típicos según BPH:

| BPH | Amplitud sana |
|---|---|
| 18000 | 180° – 250° |
| 21600 | 220° – 300° |
| 28800 | 250° – 310° |

- **Baja amplitud** (< 150–180°): desgaste de pivotes/rubíes, aceite viejo,
  o el reloj sin cuerda completa.
- **Alta amplitud** (> 330°): sobreoscilación (knocking) — peligro de golpe
  del escape.
- La app muestra la amplitud estimada **aunque esté fuera del rango físico**
  (135°–360°) marcada con un asterisco (`100*`) — es información de
  diagnóstico: una amplitud baja indica desgaste o falta de cuerda. Solo
  muestra `---` cuando ni siquiera logra medir los pulsos.

### Lift angle (grados)

El **ángulo de elevación**: el arco durante el cual el escape recibe el
impulso del volante. Es un parámetro del calibre (típicamente 44°–56°;
default 52°). Se usa para calcular la amplitud; si no conoces el valor
exacto de tu calibre, el error en la amplitud será proporcional, pero el
rate y el beat error no se ven afectados.

### Calibration (s/d) — la frecuencia real del micrófono

Algunas tarjetas de sonido USB no producen exactamente 44100 Hz. La
calibración mide la **desviación real del sample rate** (~15 min) y lo
compensa en todos los cálculos de rate. El valor se muestra en el control
`cal` (en décimas de s/d).

- Si `cal = 0` y tu tarjeta está desviada, verás un rate falso (p. ej. +50
  s/d en un reloj sano). Esa desviación se suma a la medición real.
- **Flujo**: reloj en marcha sobre el mic → menú Command → *Calibrate* →
  esperar el progreso → el valor se aplica automáticamente.

### Signal — la detección

La app indica si está consiguiendo una medición válida:

- **Signal = 1**: hay una detección estable (el valor en pantalla es
  fiable).
- **Signal = 0**: no se detecta un tic periódico — revisa: ¿el reloj tiene
  cuerda? ¿está apoyado en el micrófono? ¿la señal no está recortando
  (CLIP) o demasiado baja?

## 3. Configuración práctica

### Gain (ganancia)

Rango **0.1–100**: 1.0 = sin cambio, < 1 atenúa, > 1 amplifica.

- **Regla**: ajusta para que la barra de nivel muestre un **pico ≈ 0.6–0.9**
  y **sin CLIP**.
- Pico > 0.98 (CLIP): la señal satura y se distorsiona — baja el gain o el
  nivel de entrada del sistema.
- Pico < 0.2: la señal puede ser demasiado débil para una medición estable.
- Con un micrófono de timegrapher (p. ej. TGBC, con amplificador integrado)
  suele bastar gain **0.3–0.7**.

### Cutoff (frecuencia de corte del filtro)

El filtro pasabanda se centra en esta frecuencia (default **3000 Hz**).

- El **high-pass** elimina el ruido de baja frecuencia (zumbido, ambiente).
- El **low-pass** (tras rectificar) suaviza la envolvente del tic.
- El valor ideal es **donde resuena tu micrófono** (los piezo suelen resonar
  en 1–5 kHz). Para encontrarlo, mide el mismo reloj con cutoff 2000 / 3000 /
  4000 y quédate con el que dé menor `sigma` (medición más estable).
- Cambiar el cutoff reinicia la adquisición (se vacía la cinta).

### Dispositivo de entrada

- Selecciona el micrófono en el selector **mic**; si lo reconectaste y no
  aparece, usa el botón **↻**.
- Si el dispositivo guardado no se encuentra al arrancar, la app avisa y usa
  el predeterminado del sistema.

### Light algorithm

Reduce la tasa de muestreo a la mitad (22050 Hz) para aligerar el cómputo.
Útil en máquinas lentas; en relojes normales no hace falta.

## 4. El panel

- **Lectura grande**: rate, beat error, amplitud, BPH adivinado, e indicador
  de señal (color del icono).
- **Paperstrip (cinta)**: los puntos de cada tic caen en una línea; la
  pendiente de la línea es el rate. Botones `<`, `>`, `Center`, `Clear`.
- **Formas de onda tic/toc**: el pulso plegado de cada golpe (con marcadores
  de 3 fases unlock/impulso/drop cuando se detectan).
- **Forma de onda de periodo**: la oscilación completa con las ventanas de
  tic y toc sombreadas.
- **Espectro**: análisis de frecuencias en vivo de la entrada.
- **Tendencia**: el rate de los últimos ~5 minutos con media, σ, mín y máx.
- **Posición (pos)**: etiqueta las mediciones con la posición del reloj.

## 5. Grabación y análisis offline

- **Grabar**: Command → *Record to file...* guarda el audio del micrófono en
  un WAV; *Stop recording* lo finaliza.
- **Abrir grabación**: Command → *Open recording...* analiza un WAV sin
  micrófono (la cinta se reproduce a velocidad real).
- **Headless**: `tg-timer-dbg analyze archivo.wav` imprime
  signal/bph/rate/be/amp y sale (para automatizar y para pruebas).
- **Comprobar el nivel de una grabación**:
  `python3 tests/check_level.py archivo.wav` (clip%, peak, RMS y
  autocorrelación de la envolvente — un pico claro ~0.3–0.6 s indica un tic
  periódico).

## 6. Session log (depuración)

Command → *Save session log* guarda en `~/tg-logs/` tres archivos por sesión:

- **`.json`** — un objeto por ciclo de cómputo (wall_ms, audio, signal, bph,
  rate, be, amp, period, sigma, calibrate, cal_state).
- **`.csv`** — la misma información en tabla (para Excel/scripts).
- **`.raw`** — el texto de debug completo con timestamp.

En consola: `tg-timer debug` (resumen por ciclo) o `tg-timer debug full`
(todos los detalles de detección).

## 7. Posiciones e informe

1. Mide 30–60 s en una posición (p. ej. dial up) con el selector **pos** en
   esa posición.
2. Repite para las otras posiciones.
3. Command → *Export report...* → escribe en `~/tg-logs/` el
   `tg-report-<timestamp>.{csv,pdf}` con el resumen por posición: n, media,
   σ, mín, máx del rate + media de be y amp.

**Interpretación del informe**: la diferencia entre la mejor y la peor
posición (Δ entre media máx y mín) indica el estado de la regulación. Un
reloj sano suele variar < 15–20 s/d entre posiciones; variaciones grandes
apuntan a pivotes, hairspring o balance en mal estado.

## 8. Resolución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| Espectro se mueve pero sin datos | Reloj sin cuerda, mal asentado, señal débil/recortada | Dar cuerda, apoyar el reloj en la cavidad, ajustar gain (sin CLIP), reducir ruido |
| "Saved input device ... not found; falling back to default" | El mic fue reconectado (cambió `hw:X,Y`) | Seleccionar de nuevo el dispositivo en **mic** (botón ↻) |
| CLIP encendido | Señal satura | Bajar gain o el nivel de entrada del sistema |
| Rate raro (~+50 s/d) en un reloj sano | Sample rate real de la tarjeta desviado | Ejecutar *Calibrate* (~15 min) |
| BPH lento (12000/14400) no detecta | Limitación: periodos ≥ 0.5 s a 44.1 kHz | No disponible por ahora |
| Report export vacío | Ciclos sin posición etiquetada | Seleccionar "pos" o usar la fila "none"/total (arreglado) |