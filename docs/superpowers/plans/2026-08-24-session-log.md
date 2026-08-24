# Session Log (JSON/CSV/raw) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Añadir "Save session log" al menú Command: un clic guarda en `~/tg-logs/` tres archivos por sesión — `.json` (ciclos estructurados), `.csv` (tabular) y `.raw` (texto de debug con timestamp) — para depurar la detección sin terminal. Funciona en builds release y debug.

**Architecture:** Nuevo módulo `src/session.c/.h` autónomo (solo pthread + stdlib, como `wav.c`): un anillo de registros por ciclo de cómputo (`struct session_cycle`, tope 50 000 ≈ 80 min) y un anillo de líneas raw con timestamp (tope 20 000). La captura se hace en `refresh()` (interface.c) — un registro por ciclo publicado — y `debug()` pasa a estar siempre activo (macro incondicional) volcando a stderr solo en DEBUG y al anillo raw en todos los builds. El ítem de menú crea `~/tg-logs/`, genera base con `strftime` y escribe los 3 archivos.

**Tech Stack:** C99, pthread, GTK3 (solo UI), autotools (`make check`).

**Rama:** `feature/session-log` (creada desde `master`, que ya incluye Fase 1).

---

## Estructura de archivos

- Create: `src/session.h`, `src/session.c`, `tests/test_session.c`
- Modify: `src/tg.h` (macro `debug` incondicional), `src/interface.c` (print_debug tee, captura en refresh, menú + handler, session_init), `Makefile.am` (sources + tests), `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

---

### Task 1: Módulo `session.c/.h` + tests (`make check`)

**Files:**
- Create: `src/session.h`, `src/session.c`, `tests/test_session.c`
- Modify: `Makefile.am`

- [ ] **Step 1: Escribir el test (falla por falta del módulo)**

Crea `tests/test_session.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "session.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	session_init();

	struct session_cycle c1 = { 1000, 1000, 4, 0, 0, 0,
		1.234567, 0.500000, 250.000000, 0.328160, 0.000038 };
	struct session_cycle c2 = { 2000, 2000, 0, 0, 0, 0,
		0.0, 0.0, 0.0, 0.0, 0.0 };
	session_add_cycle(&c1);
	session_add_cycle(&c2);
	session_add_raw(1000, "START OF COMPUTATION CYCLE\n");
	session_add_raw(2000, "no candidate period\n");

	const char *dir = "test_session_out";
	mkdir(dir, 0755);

	CHECK(session_save(dir, "test_session") == 0, "save ok");

	/* JSON */
	{
		char json_path[512], csv_path[512], raw_path[512];
		snprintf(json_path, sizeof(json_path), "%s/%s.json", dir, "test_session");
		snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", dir, "test_session");
		snprintf(raw_path, sizeof(raw_path), "%s/%s.raw", dir, "test_session");
		char buf[8192] = {0};
		FILE *f = fopen(json_path, "r");
		CHECK(f != NULL, "json exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "\"wall_ms\":1000") != NULL, "json c1 wall");
			CHECK(strstr(buf, "\"rate\":1.234567") != NULL, "json c1 rate");
			CHECK(strstr(buf, "\"period\":0.328160") != NULL, "json c1 period");
			CHECK(strstr(buf, "\"wall_ms\":2000") != NULL, "json c2 wall");
		}
	}

	/* CSV */
	{
		char json_path[512], csv_path[512], raw_path[512];
		snprintf(json_path, sizeof(json_path), "%s/%s.json", dir, "test_session");
		snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", dir, "test_session");
		snprintf(raw_path, sizeof(raw_path), "%s/%s.raw", dir, "test_session");
		char buf[2048] = {0};
		FILE *f = fopen(csv_path, "r");
		CHECK(f != NULL, "csv exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "wall_ms,audio,signal,bph,rate,be,amp,period,sigma,calibrate,cal_state") != NULL,
			      "csv header");
			CHECK(strstr(buf, "1000,1000,4,0,1.234567,0.500000,250.000000,0.328160,0.000038,0,0") != NULL,
			      "csv c1 row");
		}
	}

	/* RAW */
	{
		char json_path[512], csv_path[512], raw_path[512];
		snprintf(json_path, sizeof(json_path), "%s/%s.json", dir, "test_session");
		snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", dir, "test_session");
		snprintf(raw_path, sizeof(raw_path), "%s/%s.raw", dir, "test_session");
		char buf[1024] = {0};
		FILE *f = fopen(raw_path, "r");
		CHECK(f != NULL, "raw exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "[1000] START OF COMPUTATION CYCLE") != NULL, "raw line 1");
			CHECK(strstr(buf, "[2000] no candidate period") != NULL, "raw line 2");
		}
	}

	remove(dir "/test_session.json");
	remove(dir "/test_session.csv");
	remove(dir "/test_session.raw");
	rmdir(dir);

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("session tests passed\n");
	return 0;
}
```

- [ ] **Step 2: Ejecutar para verlo fallar**

```bash
make check 2>&1 | tail -15
```

Expected: falla (falta `session.h` / funciones).

- [ ] **Step 3: Implementar `src/session.h`**

```c
/*
    tg
    ... (cabecera GPL-2 idéntica a src/wav.h) ...
*/

#ifndef TG_SESSION_H
#define TG_SESSION_H

#include <stdint.h>

struct session_cycle {
	uint64_t wall_ms;    /* reloj de pared, ms desde epoch */
	uint64_t audio;      /* get_timestamp() (muestras del ring) */
	int signal;          /* nº de pasos listos / señal (0 = nada) */
	int guessed_bph;
	int calibrate;
	int cal_state;       /* estado de calibración (0..1) */
	double rate;         /* s/d */
	double be;           /* ms */
	double amp;          /* deg (0 = no disponible) */
	double period;       /* s */
	double sigma;        /* s */
};

/* Debe llamarse una vez al arrancar. */
void session_init(void);

/* Un registro por ciclo de cómputo publicado (anillo ~80 min). */
void session_add_cycle(const struct session_cycle *c);

/* Línea de texto crudo con timestamp (anillo ~20 000 líneas). */
void session_add_raw(uint64_t wall_ms, const char *line);

/* Escribe <dir>/<base>.{json,csv,raw}. Devuelve el nº de archivos con error (0 = ok). */
int session_save(const char *dir, const char *base);

#endif
```

- [ ] **Step 4: Implementar `src/session.c`**

```c
/*
    tg
    ... (cabecera GPL-2 idéntica a src/wav.c) ...
*/

#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define SESSION_CYCLES 50000
#define SESSION_RAW_LINES 20000
#define SESSION_RAW_LEN 160

static struct {
	pthread_mutex_t m;
	struct session_cycle cycles[SESSION_CYCLES];
	int cycles_n, cycles_wp;
	struct {
		uint64_t wall_ms;
		char text[SESSION_RAW_LEN];
	} raw[SESSION_RAW_LINES];
	int raw_n, raw_wp;
} ses;

void session_init(void)
{
	pthread_mutex_init(&ses.m, NULL);
	ses.cycles_n = ses.cycles_wp = 0;
	ses.raw_n = ses.raw_wp = 0;
}

void session_add_cycle(const struct session_cycle *c)
{
	pthread_mutex_lock(&ses.m);
	ses.cycles[ses.cycles_wp] = *c;
	ses.cycles_wp = (ses.cycles_wp + 1) % SESSION_CYCLES;
	if(ses.cycles_n < SESSION_CYCLES) ses.cycles_n++;
	pthread_mutex_unlock(&ses.m);
}

void session_add_raw(uint64_t wall_ms, const char *line)
{
	pthread_mutex_lock(&ses.m);
	strncpy(ses.raw[ses.raw_wp].text, line, SESSION_RAW_LEN - 1);
	ses.raw[ses.raw_wp].text[SESSION_RAW_LEN - 1] = 0;
	ses.raw[ses.raw_wp].wall_ms = wall_ms;
	ses.raw_wp = (ses.raw_wp + 1) % SESSION_RAW_LINES;
	if(ses.raw_n < SESSION_RAW_LINES) ses.raw_n++;
	pthread_mutex_unlock(&ses.m);
}

/* Índice del registro más antiguo (para escribir en orden cronológico). */
static int cycle_start(void)
{
	return ses.cycles_n < SESSION_CYCLES ? 0 : ses.cycles_wp;
}

static int raw_start(void)
{
	return ses.raw_n < SESSION_RAW_LINES ? 0 : ses.raw_wp;
}

static void write_json(FILE *f)
{
	int start = cycle_start();
	int i;
	fprintf(f, "{\n  \"cycles\": [\n");
	for(i = 0; i < ses.cycles_n; i++) {
		const struct session_cycle *c = &ses.cycles[(start + i) % SESSION_CYCLES];
		fprintf(f,
			"    {\"wall_ms\":%llu,\"audio\":%llu,\"signal\":%d,\"bph\":%d,"
			"\"rate\":%.6f,\"be\":%.6f,\"amp\":%.6f,\"period\":%.6f,\"sigma\":%.6f,"
			"\"calibrate\":%d,\"cal_state\":%d}%s\n",
			(unsigned long long)c->wall_ms, (unsigned long long)c->audio,
			c->signal, c->guessed_bph, c->rate, c->be, c->amp, c->period, c->sigma,
			c->calibrate, c->cal_state,
			i + 1 < ses.cycles_n ? "," : "");
	}
	fprintf(f, "  ]\n}\n");
}

static void write_csv(FILE *f)
{
	int start = cycle_start();
	int i;
	fprintf(f, "wall_ms,audio,signal,bph,rate,be,amp,period,sigma,calibrate,cal_state\n");
	for(i = 0; i < ses.cycles_n; i++) {
		const struct session_cycle *c = &ses.cycles[(start + i) % SESSION_CYCLES];
		fprintf(f, "%llu,%llu,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d\n",
			(unsigned long long)c->wall_ms, (unsigned long long)c->audio,
			c->signal, c->guessed_bph, c->rate, c->be, c->amp, c->period, c->sigma,
			c->calibrate, c->cal_state);
	}
}

static void write_raw(FILE *f)
{
	int start = raw_start();
	int i;
	for(i = 0; i < ses.raw_n; i++) {
		int idx = (start + i) % SESSION_RAW_LINES;
		fprintf(f, "[%llu] %s", (unsigned long long)ses.raw[idx].wall_ms, ses.raw[idx].text);
	}
}

int session_save(const char *dir, const char *base)
{
	char path[1024];
	int errs = 0;
	FILE *f;

	pthread_mutex_lock(&ses.m);

	snprintf(path, sizeof(path), "%s/%s.json", dir, base);
	f = fopen(path, "w");
	if(f) { write_json(f); if(fclose(f)) errs++; } else errs++;

	snprintf(path, sizeof(path), "%s/%s.csv", dir, base);
	f = fopen(path, "w");
	if(f) { write_csv(f); if(fclose(f)) errs++; } else errs++;

	snprintf(path, sizeof(path), "%s/%s.raw", dir, base);
	f = fopen(path, "w");
	if(f) { write_raw(f); if(fclose(f)) errs++; } else errs++;

	pthread_mutex_unlock(&ses.m);
	return errs;
}
```

- [ ] **Step 5: Registrar en `Makefile.am` y verificar**

Añade `src/session.c` a `tg_timer_SOURCES` (junto a `src/wav.c`) y amplía los tests:

```make
check_PROGRAMS = tests/test_wav tests/test_session
tests_test_wav_SOURCES = tests/test_wav.c src/wav.c
tests_test_wav_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_session_SOURCES = tests/test_session.c src/session.c
tests_test_session_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_session_LDADD = -lpthread
TESTS = tests/test_wav tests/test_session
```

```bash
autoreconf -i 2>&1 | tail -3 && ./configure 2>&1 | tail -2
make check 2>&1 | tail -12
```

Expected: `PASS: tests/test_wav` y `PASS: tests/test_session`; 0 FAIL.

- [ ] **Step 6: Commit**

```bash
git add src/session.h src/session.c tests/test_session.c Makefile.am
git commit -m "Add session log module (JSON/CSV/raw) with tests"
```

---

### Task 2: `debug()` siempre activo + captura de ciclos en `refresh()`

**Files:**
- Modify: `src/tg.h`, `src/interface.c`

- [ ] **Step 1: Macro `debug()` incondicional en `src/tg.h`**

Reemplaza el bloque actual (tg.h:71-75):

```c
#ifdef DEBUG
#define debug(...) print_debug(__VA_ARGS__)
#else
#define debug(...) {}
#endif
```

por:

```c
/* Siempre activo: vierte al anillo de sesión (raw) en todos los builds y a
 * stderr solo en DEBUG. */
#define debug(...) print_debug(__VA_ARGS__)
```

- [ ] **Step 2: `print_debug` hace tee al anillo raw (`src/interface.c`)**

Reemplaza `print_debug` (interface.c:32-38):

```c
void print_debug(char *format,...)
{
	va_list args;
	va_start(args,format);
	char buf[768];
	vsnprintf(buf,sizeof(buf),format,args);
	va_end(args);
#ifdef DEBUG
	fputs(buf,stderr);
#endif
	session_add_raw(g_get_real_time() / 1000, buf);
}
```

Añade `#include "session.h"` y `#include <time.h>` a los includes de `src/interface.c`.

- [ ] **Step 3: `session_init()` y captura de ciclos en `refresh()`**

En `start_interface` (interface.c), tras `memset(w, 0, sizeof(struct main_window));` añade:

```c
	session_init();
```

En `refresh()` (interface.c:979-1010), tras `op_set_snapshot(w->active_panel, w->active_snapshot);` añade:

```c
	struct snapshot *sn = w->active_snapshot;
	struct session_cycle sc;
	memset(&sc, 0, sizeof(sc));
	sc.wall_ms = g_get_real_time() / 1000;
	sc.audio = get_timestamp(sn->is_light);
	sc.signal = sn->signal;
	sc.guessed_bph = sn->guessed_bph;
	sc.rate = sn->rate;
	sc.be = sn->be;
	sc.amp = sn->amp;
	sc.calibrate = sn->calibrate;
	sc.cal_state = sn->cal_state;
	if(sn->pb) {
		sc.period = sn->pb->period / sn->pb->sample_rate;
		sc.sigma = sn->pb->sigma / sn->pb->sample_rate;
	}
	session_add_cycle(&sc);
```

- [ ] **Step 4: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build done"
make tg-timer-dbg 2>&1 | grep -iE "warning|error"; echo "build dbg done"
make check 2>&1 | tail -8
make test 2>&1 | tail -5
```

Expected: sin warnings nuevos; `make check` PASS; `make test` (smoke 3 s) termina limpio.

- [ ] **Step 5: Commit**

```bash
git add src/tg.h src/interface.c
git commit -m "Capture session cycles and tee debug output to session ring"
```

---

### Task 3: Ítem de menú "Save session log"

**Files:**
- Modify: `src/interface.c`

- [ ] **Step 1: Handler**

Añade tras `handle_stop_recording`:

```c
static void handle_save_session_log(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	char *dir = g_build_filename(g_get_home_dir(), "tg-logs", NULL);
	if(g_mkdir_with_parents(dir, 0755)) {
		error("Cannot create log directory %s", dir);
		g_free(dir);
		return;
	}
	char base[64];
	time_t t = time(NULL);
	strftime(base, sizeof(base), "tg-session-%Y%m%d-%H%M%S", localtime(&t));
	int err = session_save(dir, base);
	if(err)
		error("Session log: %d file(s) failed in %s", err, dir);
	else {
		GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->window), 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			"Session log saved to\n%s/%s.{json,csv,raw}", dir, base);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	g_free(dir);
}
```

(Requiere `#include <time.h>`, ya añadido en Task 2 Step 2.)

- [ ] **Step 2: Ítem de menú**

En `init_main_window`, tras el ítem "Save all snapshots" (interface.c:923) y antes del separador, añade:

```c
	// ... Save session log
	GtkWidget *session_item = gtk_menu_item_new_with_label("Save session log");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), session_item);
	g_signal_connect(session_item, "activate", G_CALLBACK(handle_save_session_log), w);
```

- [ ] **Step 3: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build done"
make test 2>&1 | tail -5
```

Expected: sin warnings; smoke test limpio.

- [ ] **Step 4: Commit**

```bash
git add src/interface.c
git commit -m "Add Save session log menu item"
```

---

### Task 4: Verificación funcional del log

**Files:** ninguno (verificación)

- [ ] **Step 1: Regresión del camino `--analyze`**

```bash
./tg-timer-dbg analyze /tmp/opencode/tick.wav
```

Expected: `signal 1`, `bph 18000` (el `debug()` incondicional no rompe el análisis).

- [ ] **Step 2: Prueba de clic real (usuario)**

El ítem de menú solo escribe al hacer clic. Pedir al usuario:

1. Abrir la app (cualquier binario).
2. Menú Command → *Save session log*.
3. Comprobar:

```bash
ls -la ~/tg-logs/
head -c 600 ~/tg-logs/*.json
head -5 ~/tg-logs/*.csv
head -5 ~/tg-logs/*.raw
```

Expected: `tg-session-<timestamp>.{json,csv,raw}` con objetos por ciclo (incl. ciclos con `"signal":0` si la detección no engancha) y líneas raw `[<wall_ms>] ...`.

- [ ] **Step 3: Commit (si hay ajustes) o pasar directamente**

Si todo funciona, no hay commit. Si algo falla, corregir y commitear.

---

### Task 5: Docs + cierre

**Files:**
- Modify: `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

- [ ] **Step 1: Documentar**

En `docs/ROADMAP.md`, añade una sección:

```markdown
## Fase 1.5 — Session log (JSON/CSV/raw)

**Rama:** `feature/session-log` — **Estado:** implementada.

Menú Command → *Save session log*: guarda en `~/tg-logs/` tres archivos por
sesión (`tg-session-<timestamp>.{json,csv,raw}`) con los ciclos de cómputo
(timestamp, signal, bph, rate, be, amp, period, sigma) y el texto de debug
crudo. Funciona en builds release y debug.
```

En `README.md`, tras la sección de recording, añade:

```markdown
## Session log

Command menu → *Save session log* writes the session's computation cycles to
`~/tg-logs/tg-session-<timestamp>.{json,csv,raw}` (structured cycles + raw
debug text), in both release and debug builds.
```

En `docs/tg-timer.1`, añade una línea:

```
.SH SESSION LOG
The \fBSave session log\fP menu item writes the computation cycles to
\fB~/tg-logs/tg-session-<timestamp>.{json,csv,raw}\fP for debugging.
```

- [ ] **Step 2: Verificación completa y commit final**

```bash
make check 2>&1 | tail -6
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
```

```bash
git add -A
git commit -m "Document session log feature"
git push origin feature/session-log
```

- [ ] **Step 3: Integrar a `master` (tras aprobación)**

```bash
git checkout master && git merge feature/session-log && git push origin master
git branch -d feature/session-log
git push origin --delete feature/session-log
```

---

## Self-review

- **Cobertura:** menú → Task 3; 3 formatos → Task 1; timestamp en archivos y registros → Task 1 (wall_ms) + Task 3 (strftime); funciona en release → Task 2 (macro incondicional) + Task 4 verificación; carpeta fija `~/tg-logs` → Task 3.
- **Sin placeholders:** todo el código está incluido.
- **Consistencia de tipos:** `session_cycle` usa los mismos nombres de campo en session.c, interface.c y el test. `session_save(dir, base)` devuelve nº de errores; `session_init()` se llama una vez en `start_interface`; `debug()` incondicional requiere que `print_debug` exista en ambos builds (ya es así, está en interface.c sin `#ifdef`).