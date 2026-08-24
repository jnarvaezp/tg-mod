# Estadísticas y Tendencia — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Añadir estadísticas en vivo del rate (media, σ, min, max) y un gráfico de tendencia temporal (rate vs tiempo) junto al paperstrip.

**Architecture:** Nuevo módulo `src/stats.c/.h` autónomo (solo pthread + stdlib, como `session.c`): un anillo de puntos por ciclo (`wall_ms`, `rate`, `be`, `amp`; tope 36 000 ≈ 60 min), `stats_summary(window_ms)` (media/σ/min/max con corrección n−1) y `stats_get_range(from_ms, out, max)` para dibujar. `refresh()` (interface.c) alimenta un punto por ciclo junto al session capture; el panel de tendencia (nueva `GtkDrawingArea` de ~60 px bajo el espectro) se redibuja con el notebook en cada refresh y pinta la línea del rate, cuadrícula centrada en 0, y el texto de estadísticas. El botón "Clear" resetea también el anillo de stats.

**Tech Stack:** C99, GTK3/Cairo, pthread, autotools `make check`.

**Rama:** `feature/stats-trend` (creada desde `master`).

---

## Estructura de archivos

- Create: `src/stats.h`, `src/stats.c`, `tests/test_stats.c`
- Modify: `src/output_panel.c` (área de tendencia + draw), `src/interface.c` (stats_add en refresh, stats_init), `src/tg.h` (campo trend_drawing_area), `Makefile.am`, `.gitignore`
- Docs: `docs/ROADMAP.md`, `README.md`

---

### Task 1: Módulo `stats` + tests

**Files:** Create `src/stats.h`, `src/stats.c`, `tests/test_stats.c`; Modify `Makefile.am`, `.gitignore`

- [ ] **Step 1: Escribir el test (falla por falta del módulo)**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stats.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	stats_init();

	/* 5 puntos con rate 0,1,2,3,4 */
	int i;
	struct stats_point p = { 0, 0, 0, 0 };
	for(i = 0; i < 5; i++) {
		p.wall_ms = 1000 + i;
		p.rate = i;
		stats_add(&p);
	}

	struct stats_summary s;
	CHECK(stats_summary(0, &s) == 5, "count");
	CHECK(fabs(s.mean - 2.0) < 1e-9, "mean");
	CHECK(fabs(s.min - 0.0) < 1e-9, "min");
	CHECK(fabs(s.max - 4.0) < 1e-9, "max");
	CHECK(fabs(s.sigma - sqrt(2.5)) < 1e-9, "sigma (n-1)");

	/* ventana: solo los últimos 2 (wall_ms 1003,1004) */
	CHECK(stats_summary(1000, &s) == 2, "window count");
	CHECK(fabs(s.mean - 3.5) < 1e-9, "window mean");

	/* rango: copia desde 1003 -> 2 puntos en orden */
	struct stats_point out[8];
	int n = stats_get_range(1003, out, 8);
	CHECK(n == 2 && out[0].rate == 3.0 && out[1].rate == 4.0, "get_range");

	stats_clear();
	CHECK(stats_summary(0, &s) == 0, "cleared");

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("stats tests passed\n");
	return 0;
}
```

- [ ] **Step 2: Ejecutar para verlo fallar**

```bash
make check 2>&1 | tail -8
```

Expected: falla (falta `stats.h`).

- [ ] **Step 3: Implementar `src/stats.h`**

```c
/*
    tg
    ... (cabecera GPL-2 idéntica a src/wav.h) ...
*/

#ifndef TG_STATS_H
#define TG_STATS_H

#include <stdint.h>

struct stats_point {
	uint64_t wall_ms;   /* reloj de pared, ms desde epoch */
	double rate;        /* s/d */
	double be;          /* ms */
	double amp;         /* deg (0 = no disponible) */
};

struct stats_summary {
	int n;
	double mean;        /* s/d */
	double sigma;       /* desviación estándar (n-1), s/d */
	double min;
	double max;
};

void stats_init(void);
void stats_add(const struct stats_point *p);

/* Resumen del rate sobre los últimos window_ms (0 = todos). Devuelve n. */
int stats_summary(uint64_t window_ms, struct stats_summary *out);

/* Copia en `out` (máx `max`) los puntos con wall_ms >= from_ms, en orden
 * cronológico. Devuelve el nº copiado. */
int stats_get_range(uint64_t from_ms, struct stats_point *out, int max);

void stats_clear(void);

#endif
```

- [ ] **Step 4: Implementar `src/stats.c`**

```c
/*
    tg
    ... (cabecera GPL-2 idéntica a src/wav.c) ...
*/

#include "stats.h"
#include <pthread.h>
#include <math.h>

#define STATS_CAP 36000   /* ~60 min a 10 ciclos/s */

static struct {
	pthread_mutex_t m;
	struct stats_point pts[STATS_CAP];
	int n, wp;
} st;

void stats_init(void)
{
	pthread_mutex_init(&st.m, NULL);
	st.n = st.wp = 0;
}

void stats_add(const struct stats_point *p)
{
	pthread_mutex_lock(&st.m);
	st.pts[st.wp] = *p;
	st.wp = (st.wp + 1) % STATS_CAP;
	if(st.n < STATS_CAP) st.n++;
	pthread_mutex_unlock(&st.m);
}

int stats_summary(uint64_t window_ms, struct stats_summary *out)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	uint64_t oldest = st.n ? st.pts[start].wall_ms : 0;
	double sum = 0, sq = 0, mn = 0, mx = 0;
	int cnt = 0, i;
	for(i = 0; i < st.n; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(window_ms && p->wall_ms > oldest + window_ms) continue;
		if(!cnt || p->rate < mn) mn = p->rate;
		if(!cnt || p->rate > mx) mx = p->rate;
		sum += p->rate;
		sq += p->rate * p->rate;
		cnt++;
	}
	if(out) {
		out->n = cnt;
		if(cnt) {
			out->mean = sum / cnt;
			out->sigma = cnt > 1 ? sqrt((sq - cnt * out->mean * out->mean) / (cnt - 1)) : 0;
			out->min = mn;
			out->max = mx;
		} else
			out->mean = out->sigma = out->min = out->max = 0;
	}
	pthread_mutex_unlock(&st.m);
	return cnt;
}

int stats_get_range(uint64_t from_ms, struct stats_point *out, int max)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	int cnt = 0, i;
	for(i = 0; i < st.n && cnt < max; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(p->wall_ms < from_ms) continue;
		out[cnt++] = *p;
	}
	pthread_mutex_unlock(&st.m);
	return cnt;
}

void stats_clear(void)
{
	pthread_mutex_lock(&st.m);
	st.n = st.wp = 0;
	pthread_mutex_unlock(&st.m);
}
```

- [ ] **Step 5: Registrar en `Makefile.am` y `.gitignore`**

Añade `src/stats.c` a `tg_timer_SOURCES` (junto a `src/session.c`):

```make
check_PROGRAMS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats
tests_test_stats_SOURCES = tests/test_stats.c src/stats.c
tests_test_stats_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_stats_LDADD = -lpthread -lm
TESTS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats
```

`.gitignore`: añade `/tests/test_stats`.

```bash
autoreconf -i 2>&1 | tail -2 && ./configure 2>&1 | tail -2
make check 2>&1 | tail -12
```

Expected: 4 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/stats.h src/stats.c tests/test_stats.c Makefile.am .gitignore
git commit -m "Add live stats module (mean/sigma/min/max) with tests"
```

---

### Task 2: Gráfico de tendencia en el panel

**Files:** Modify `src/output_panel.c`, `src/tg.h`

- [ ] **Step 1: Campo en `struct output_panel` (`src/tg.h`)**

Junto a `paperstrip_drawing_area`:

```c
	GtkWidget *trend_drawing_area;
```

- [ ] **Step 2: Draw event en `src/output_panel.c`**

Añade `#include "stats.h"` a los includes. Añade tras `spectrum_draw_event`:

```c
#define TREND_HEIGHT 60
#define TREND_MAX_POINTS 3000   /* ~5 min a 10 ciclos/s */

static void trend_draw_event(GtkWidget *w, cairo_t *c, struct output_panel *op)
{
	UNUSED(op);
	GtkAllocation temp;
	gtk_widget_get_allocation(w, &temp);
	int width = temp.width;
	int height = temp.height;
	if(width < 20 || height < 20) return;

	cairo_set_source_rgb(c, 1, 1, 1);
	cairo_paint(c);

	struct stats_point pts[TREND_MAX_POINTS];
	int n = stats_get_range(0, pts, TREND_MAX_POINTS);
	if(n < 2) return;

	double maxabs = 10;
	int i;
	for(i = 0; i < n; i++) {
		double a = fabs(pts[i].rate);
		if(a > maxabs) maxabs = a;
	}
	maxabs *= 1.15;
	if(maxabs < 10) maxabs = 10;

	double mid = height / 2.0;
	double scale = (height - 20) / 2.0 / maxabs;

	/* cuadrícula: centro 0 y límites ±maxabs */
	cairo_set_line_width(c, 1);
	cairo_set_source_rgb(c, 0.85, 0.85, 0.85);
	cairo_move_to(c, 0, mid);
	cairo_line_to(c, width, mid);
	cairo_stroke(c);
	cairo_set_source_rgb(c, 0.92, 0.92, 0.92);
	cairo_move_to(c, 0, mid - scale * maxabs);
	cairo_line_to(c, width, mid - scale * maxabs);
	cairo_move_to(c, 0, mid + scale * maxabs);
	cairo_line_to(c, width, mid + scale * maxabs);
	cairo_stroke(c);

	/* línea del rate */
	cairo_set_source_rgb(c, 0, 0.6, 0);
	cairo_set_line_width(c, 1.5);
	cairo_move_to(c, 0, mid - pts[0].rate * scale);
	for(i = 1; i < n; i++) {
		double x = (double)i / (n - 1) * width;
		cairo_line_to(c, x, mid - pts[i].rate * scale);
	}
	cairo_stroke(c);

	/* texto de estadísticas */
	struct stats_summary s;
	stats_summary(0, &s);
	char txt[128];
	snprintf(txt, sizeof(txt), "n=%d  media=%.1f  sigma=%.1f  min=%.1f  max=%.1f s/d",
	         s.n, s.mean, s.sigma, s.min, s.max);
	cairo_set_source_rgb(c, 0, 0, 0);
	cairo_select_font_face(c, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, 11);
	cairo_move_to(c, 4, height - 4);
	cairo_show_text(c, txt);
}
```

- [ ] **Step 3: Crear el área en `init_output_panel` (tras el espectro, antes de hbox3)**

```c
	// Trend chart (rate over time)
	op->trend_drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(op->trend_drawing_area, 300, TREND_HEIGHT);
	gtk_box_pack_start(GTK_BOX(vbox2), op->trend_drawing_area, FALSE, TRUE, 0);
	g_signal_connect(op->trend_drawing_area, "draw", G_CALLBACK(trend_draw_event), op);
	gtk_widget_set_events(op->trend_drawing_area, GDK_EXPOSURE_MASK);
```

- [ ] **Step 4: `stats_clear` en "Clear"**

En `handle_clear_trace` (output_panel.c ~772), tras `gtk_widget_queue_draw(op->paperstrip_drawing_area);` añade:

```c
		stats_clear();
```

- [ ] **Step 5: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -6
make test 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add src/output_panel.c src/tg.h
git commit -m "Add live rate trend chart with stats readout"
```

---

### Task 3: Alimentar stats desde `refresh()`

**Files:** Modify `src/interface.c`

- [ ] **Step 1: Includes e init**

Añade `#include "stats.h"` a los includes. En `start_interface`, junto a `session_init();` añade:

```c
	stats_init();
```

- [ ] **Step 2: `stats_add` en `refresh()`**

Tras el bloque de `session_add_cycle(&sc);` (interface.c, en `refresh()`) añade:

```c
	struct stats_point sp;
	memset(&sp, 0, sizeof(sp));
	sp.wall_ms = sc.wall_ms;
	sp.rate = sc.rate;
	sp.be = sc.be;
	sp.amp = sc.amp;
	stats_add(&sp);
```

- [ ] **Step 3: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -6
make test 2>&1 | tail -3
```

- [ ] **Step 4: Commit**

```bash
git add src/interface.c
git commit -m "Feed live stats from computation cycles"
```

---

### Task 4: Verificación, docs y cierre

**Files:** Modify `docs/ROADMAP.md`, `README.md`

- [ ] **Step 1: Verificación funcional**

```bash
make check 2>&1 | tail -6
timeout 10 ./tg-timer debug >/dev/null 2>&1
```

Expected: 4 tests PASS; la app abre con el gráfico de tendencia (línea verde, cuadrícula, texto `n=... media=... sigma=... min=... max=...`) que se actualiza en vivo.

- [ ] **Step 2: `docs/ROADMAP.md` — Fase 4 completada**

```markdown
## Fase 4 — Estadísticas y tendencia (completada)

**Rama:** `feature/stats-trend` — **Estado:** completada.

- Módulo `stats` (anillo ~60 min): media/σ/min/max del rate con ventana.
- Gráfico de tendencia del rate en el panel (bajo el espectro) con cuadrícula
  centrada en 0, línea del rate y resumen numérico en vivo.
- El botón Clear resetea también las estadísticas.
```

- [ ] **Step 3: `README.md`**

Añade en la sección de features (tras "Recording and offline analysis" o en una nueva "Live statistics"):

```markdown
## Live statistics

The panel shows a live rate-trend chart (last ~5 minutes) with mean, standard
deviation, min and max of the rate in s/d, updated every computation cycle.
```

- [ ] **Step 4: Verificación completa + commit + push + merge**

```bash
make check 2>&1 | tail -6
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
git add -A
git commit -m "Document live statistics and trend chart"
git push origin feature/stats-trend
git checkout master && git merge feature/stats-trend && git push origin master
git branch -d feature/stats-trend
git push origin --delete feature/stats-trend
```

---

## Self-review

- **Cobertura:** módulo + tests → Task 1; gráfico → Task 2; alimentación → Task 3; docs/merge → Task 4.
- **Enlace headless:** `stats.c` es autónomo (pthread+math); el test enlaza sin GTK.
- **Sin placeholders:** todo el código incluido.
- **Consistencia:** `stats_add/summary/get_range/clear/init` y `trend_draw_event` usan los mismos nombres en todas las tasks; el redibujado se apoya en el `queue_draw` del notebook en `refresh()` (no requiere timeouts extra).