# Signal Quality (gain attenuación + medidor + dispositivo) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dar control de nivel real al usuario con mic timegrapher (TGBC): ganancia con atenuación (0.1–100), medidor de nivel en vivo con alerta CLIP, y robustez del dispositivo guardado (emparejamiento sin sufijo `(hw:X,Y)`, aviso de fallback, refresco de lista). Verificación con `--analyze` sobre grabaciones reales.

**Architecture:** (1) Ampliar el rango de `audio_gain` (clamp 0.1–100 en `set_audio_gain` y en el handler del spin). (2) Nueva `get_audio_level(peak, rms)` en audio.c que lee ~100 ms del ring bajo mutex; un timeout de 100 ms en la UI actualiza una `GtkProgressBar` y muestra/oculta un label "CLIP" (peak > 0.98). (3) `find_input_device_by_name` compara también sin el sufijo `(hw:X,Y)`; si el guardado no se encuentra se avisa con `error()`; la población del combo se refactoriza a `populate_devices(w)` (reconstruye sin disparar `handle_device_change`, preservando selección) con un botón de refresco. (4) Verificación: `tests/check_level.py` (clip%, peak, rms, autocorrelación de envolvente) + `--analyze`; si `beat error = ---` persiste con nivel limpio, instrumentar `compute_parameters`.

**Tech Stack:** C99, GTK3, PortAudio, FFTW (existente). Autotools.

**Rama:** `feature/signal-quality` (creada desde `master`).

---

## Estructura de archivos

- Modify: `src/audio.c`, `src/interface.c`, `src/tg.h`, `src/algo.c` (solo si la instrumentación es necesaria)
- Create: `tests/check_level.py` (verificación)
- Docs: `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

---

### Task 1: Ganancia con atenuación (0.1–100)

**Files:** Modify `src/audio.c`, `src/interface.c`, `src/tg.h`

- [ ] **Step 1: Clamp en `set_audio_gain` (`src/audio.c:56-63`)**

```c
void set_audio_gain(double g)
{
	if(g < 0.1) g = 0.1;
	if(g > 100.0) g = 100.0;
	pthread_mutex_lock(&audio_mutex);
	audio_gain = g;
	pthread_mutex_unlock(&audio_mutex);
}
```

- [ ] **Step 2: Clamp en `handle_gain_change` (`src/interface.c:364-372`)**

```c
	double g = gtk_spin_button_get_value(b);
	if(g < 0.1) g = 0.1;
	if(g > 100.0) g = 100.0;
```

- [ ] **Step 3: Rango del spin (`src/interface.c:1038`)**

```c
	w->gain_spin_button = gtk_spin_button_new_with_range(0.1, 100.0, 0.1);
```

- [ ] **Step 4: Comentarios en `src/tg.h` (líneas ~175 y ~310)**

`/* Set the input gain multiplier applied in the PA callback (1.0 .. 100.0). */`
→ `(0.1 .. 100.0, puede atenuar)`.

`double gain; // audio input gain multiplier (1.0 = no amplification)` → añadir `(0.1 = 10x atenuación)`.

- [ ] **Step 5: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -4
```

- [ ] **Step 6: Commit**

```bash
git add src/audio.c src/interface.c src/tg.h
git commit -m "Allow gain attenuation (0.1-100)"
```

---

### Task 2: Medidor de nivel en vivo + CLIP

**Files:** Modify `src/audio.c`, `src/interface.c`, `src/tg.h`

- [ ] **Step 1: `get_audio_level` en `src/audio.c` (tras `get_recent_audio`)**

```c
/** Nivel de la entrada más reciente (~100 ms): pico y RMS en [0,1].
 *  Safe to call from any thread. */
int get_audio_level(float *peak, float *rms)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) file_pump_locked();
	int wp = write_pointer;
	int n = wp;
	if(n > PA_SAMPLE_RATE / 10) n = PA_SAMPLE_RATE / 10;
	float pm = 0, sum = 0;
	int i;
	for(i = 0; i < n; i++) {
		int idx = wp - n + i;
		if(idx < 0) idx += PA_BUFF_SIZE;
		float a = pa_buffers[idx];
		if(a < 0) a = -a;
		if(a > pm) pm = a;
		sum += a * a;
	}
	pthread_mutex_unlock(&audio_mutex);
	if(peak) *peak = pm;
	if(rms) *rms = n > 0 ? sqrtf(sum / n) : 0;
	return n;
}
```

En `src/tg.h`, tras `int get_recent_audio(...);`:

```c
/* Peak and RMS level of the most recent ~100 ms of input (0..1). */
int get_audio_level(float *peak, float *rms);
```

- [ ] **Step 2: Widgets en `struct main_window` (`src/tg.h`)**

Junto a `source_label`:

```c
	GtkWidget *level_bar;
	GtkWidget *clip_label;
	guint level_timeout;
```

- [ ] **Step 3: Timeout de nivel en `src/interface.c`**

```c
static gboolean update_level(struct main_window *w)
{
	float peak = 0;
	get_audio_level(&peak, NULL);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->level_bar), peak);
	if(peak > 0.98f)
		gtk_widget_show(w->clip_label);
	else
		gtk_widget_hide(w->clip_label);
	return TRUE;
}
```

En `quit()` (junto a los otros `g_source_remove`):

```c
	if(w->level_timeout) g_source_remove(w->level_timeout);
```

En `start_interface`, junto a `kick_timeout`:

```c
	w->level_timeout = g_timeout_add_full(G_PRIORITY_LOW, 100, (GSourceFunc)update_level, w, NULL);
```

- [ ] **Step 4: Widgets en `init_main_window` (tras el indicador de fuente, ~interface.c:1011)**

```c
	// Level meter
	w->level_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->level_bar), 0);
	gtk_widget_set_size_request(w->level_bar, 120, -1);
	gtk_box_pack_start(GTK_BOX(hbox), w->level_bar, FALSE, FALSE, 0);
	w->clip_label = gtk_label_new("CLIP");
	gtk_box_pack_start(GTK_BOX(hbox), w->clip_label, FALSE, FALSE, 0);
	gtk_widget_hide(w->clip_label);
```

- [ ] **Step 5: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -4
make test 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add src/audio.c src/interface.c src/tg.h
git commit -m "Add live input level meter with clipping indicator"
```

---

### Task 3: Robustez del dispositivo

**Files:** Modify `src/audio.c`, `src/interface.c`

- [ ] **Step 1: Emparejamiento sin sufijo `(hw:X,Y)` (`src/audio.c`)**

Añade antes de `find_input_device_by_name`:

```c
/* Iguala dos nombres de dispositivo, ignorando el sufijo "(hw:X,Y)"
 * (el índice ALSA cambia al reconectar el USB). */
static int device_name_matches(const char *a, const char *b)
{
	if(!strcmp(a, b)) return 1;
	const char *pa = strrchr(a, '(');
	const char *pb = strrchr(b, '(');
	if(pa && pb && pa > a && pb > b) {
		size_t la = pa - a;
		size_t lb = pb - b;
		if(la == lb && !strncmp(a, b, la))
			return 1;
	}
	return 0;
}
```

En `find_input_device_by_name`, cambia:

```c
		if(di->name && !strcmp(di->name, name))
```
por:
```c
		if(di->name && device_name_matches(di->name, name))
```

- [ ] **Step 2: Aviso visible de fallback (`src/audio.c:209-216`)**

Tras `debug("Saved input device '%s' not found; falling back to default\n", preferred);` añade:

```c
		error("Saved input device '%s' not found; using the system default", preferred);
```

- [ ] **Step 3: `populate_devices` + botón de refresco (`src/interface.c`)**

Añade la función (tras `handle_device_change`):

```c
static void populate_devices(struct main_window *w)
{
	g_signal_handlers_block_by_func(w->device_combo_box, handle_device_change, w);
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w->device_combo_box));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->device_combo_box), "System default");
	int dev_count = get_input_device_count();
	int di, active = 0;
	for(di = 0; di < dev_count; di++) {
		const char *dname = get_input_device_name(di);
		if(!dname) continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->device_combo_box), dname);
		if(w->input_device && !strcmp(w->input_device, dname))
			active = di + 1;
	}
	g_signal_handlers_unblock_by_func(w->device_combo_box, handle_device_change, w);
	gtk_combo_box_set_active(GTK_COMBO_BOX(w->device_combo_box), active);
}
```

En `init_main_window`, sustituye el bloque de población inline (interface.c:1017-1036, desde la creación del combo hasta `gtk_combo_box_set_active` incluido) por:

```c
	w->device_combo_box = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(hbox), w->device_combo_box, FALSE, FALSE, 0);
	populate_devices(w);
```

y tras el combo, añade el botón de refresco:

```c
	GtkWidget *dev_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(hbox), dev_refresh, FALSE, FALSE, 0);
	g_signal_connect_swapped(dev_refresh, "clicked", G_CALLBACK(populate_devices), w);
```

(El `g_signal_connect(w->device_combo_box, "changed", ...)` existente se mantiene después.)

(Notas de revisión Task 3:
- `populate_devices`: usar `match_input_device_name` (emparejamiento difuso) para
  la selección activa, y llamar `gtk_combo_box_set_active` MIENTRAS el handler
  está bloqueado (si se hace tras desbloquear, el combo en "System default"
  dispara `handle_device_change` y borra la preferencia).
- Guardar el botón de refresco en `w->dev_refresh` y deshabilitarlo en modo
  archivo dentro de `update_audio_mode_ui`.)

- [ ] **Step 4: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -4
make test 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add src/audio.c src/interface.c
git commit -m "Robust device matching, fallback warning and device list refresh"
```

---

### Task 4: Verificación real (mic TGBC) + instrumentación si persiste

**Files:** Create `tests/check_level.py`; Modify `src/algo.c` (solo si el paso 4 falla)

- [ ] **Step 1: `tests/check_level.py` (medidor de nivel offline)**

```python
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
```

- [ ] **Step 2: Grabación y análisis con el TGBC (usuario)**

Con el reloj **asentado en la cavidad** del TGBC y nivel sin recorte (usa el medidor nuevo + gain ≤ 1 si hace falta):

```bash
arecord -D hw:3,0 -f S16_LE -r 44100 -c 1 -d 15 /tmp/opencode/tgbc.wav   # o el hw de tu mic
python3 tests/check_level.py /tmp/opencode/tgbc.wav
./tg-timer-dbg analyze /tmp/opencode/tgbc.wav
```

Expected: `clip%` ≈ 0, autocorrelación de envolvente con pico claro ~0.33–0.4 s (norm > 0.3), `signal 1`, `bph` ≈ 18000–21600, `rate` coherente.

- [ ] **Step 3: Si `beat error = ---` persiste con nivel limpio — instrumentar `compute_parameters`**

En `src/algo.c`, antes del `peak_detector` de `tic_to_toc` (algo.c:644), añade:

```c
	debug("tic_to_toc: waveform autocorr at p/2 (win p/2 +/- %d): max = %f\n",
	      p->sample_rate / 50,
	      vmax(p->waveform_sc,
	           floor(p->period/2)-p->sample_rate/50,
	           floor(p->period/2)+p->sample_rate/50, NULL));
```

Recompilar, repetir el paso 2 y revisar el valor: si `max` es ≈ 0 o negativo → no hay tock visible a medio periodo (reloj/posición); si `max` es alto pero `peak_detector` falla → el conteo `cnt` o la forma fallan.

```bash
make tg-timer-dbg 2>&1 | grep -iE "warning|error"
git add src/algo.c && git commit -m "Instrument beat error search in compute_parameters"
```

- [ ] **Step 4: Commit si cambió algo más**

Si solo se creó `tests/check_level.py`:

```bash
git add tests/check_level.py
git commit -m "Add offline level check helper script"
```

---

### Task 5: Docs + cierre

**Files:** Modify `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

- [ ] **Step 1: `docs/ROADMAP.md` — Fase 3**

Marca la Fase 3 como (parcialmente) completada:

```markdown
## Fase 3 — Calidad de señal / diagnóstico (parcialmente completada)

**Rama:** `feature/signal-quality`

- Ganancia 0.1–100 (atenuación) y medidor de nivel en vivo con alerta CLIP.
- Emparejamiento robusto de dispositivo (ignora `(hw:X,Y)`), aviso de fallback
  y refresco de la lista.
- Pendiente: SNR/indicadores más finos, sugerencia automática de ganancia.
```

- [ ] **Step 2: `README.md`**

En la sección Debugging (o una nueva "Signal level"), añade:

```markdown
- **Input level**: the gain control (0.1–100) can attenuate or amplify the
  input; the live level bar shows the input peak and a **CLIP** indicator
  lights up when the signal saturates.
- **Device**: the saved input device is matched ignoring the ALSA
  `(hw:X,Y)` suffix, so a replugged microphone keeps working; use the
  refresh button next to the mic selector to re-enumerate devices.
```

- [ ] **Step 3: `docs/tg-timer.1`**

En la sección `RECORDING AND OFFLINE ANALYSIS` (o una nueva `INPUT LEVEL`):

```
.SH INPUT LEVEL
The gain control ranges from 0.1 (attenuation) to 100 (amplification). A
live level bar shows the input peak; a \fBCLIP\fP indicator appears when the
signal saturates. The saved input device is matched ignoring the
\fB(hw:X,Y)\fP suffix, and a refresh button next to the mic selector
re-enumerates devices.
```

- [ ] **Step 4: Verificación completa + commit final + push**

```bash
make check 2>&1 | tail -4
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
git add -A
git commit -m "Document signal quality features"
git push origin feature/signal-quality
```

- [ ] **Step 5: Integrar a `master` (tras aprobación)**

```bash
git checkout master && git merge feature/signal-quality && git push origin master
git branch -d feature/signal-quality
git push origin --delete feature/signal-quality
```

---

## Self-review

- **Cobertura:** ganancia atenuadora → Task 1; medidor+CLIP → Task 2; dispositivo (matching, aviso, refresco) → Task 3; verificación TGBC → Task 4; docs → Task 5. El volumen de captura ALSA queda fuera (YAGNI) y se documenta como pendiente en el roadmap.
- **Sin placeholders:** todo el código está incluido.
- **Consistencia:** `get_audio_level(peak, rms)`, `populate_devices(w)`, `device_name_matches(a,b)`, `update_level(w)` se usan con el mismo nombre en todas las tasks. `level_timeout` se elimina en `quit()` para evitar accesos a widgets destruidos.