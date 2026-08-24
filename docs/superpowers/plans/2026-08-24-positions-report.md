# Multi-Posición + Informe — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Etiquetar las mediciones en vivo con la posición del reloj (dial up/down, crown up/down/left/right) y exportar un informe por posición: resumen (media/σ/min/max del rate + media de be/amp) en **CSV** y **PDF** (generado con Cairo, sin dependencias nuevas).

**Architecture:** Se extiende el módulo `stats` (anillo de ciclos) con un campo `position` (0 = sin posición, 1-6 = DU/DD/CU/CD/CL/CR) y `mean_be`/`mean_amp` en el resumen, más `stats_summary_pos(pos, window, &s)`. Un combo de posición en la UI etiqueta cada ciclo (`refresh()` copia `w->position` a `sp.position`). Nuevo módulo `src/report.c/.h` (usa `stats_get_range` + cairo `pdf_surface`) con `report_summary()` (filas por posición + total), `report_write_csv()` y `report_write_pdf()`. Ítem de menú "Export report..." escribe `~/tg-logs/tg-report-<timestamp>.{csv,pdf}` (mismo patrón que el session log).

**Tech Stack:** C99, GTK3/Cairo (cairo_pdf_surface), pthread, autotools `make check`.

**Rama:** `feature/positions-report` (creada desde `master`).

---

## Estructura de archivos

- Modify: `src/stats.h`, `src/stats.c`, `tests/test_stats.c`, `src/interface.c`, `src/tg.h`, `Makefile.am`, `.gitignore`
- Create: `src/report.h`, `src/report.c`, `tests/test_report.c`
- Docs: `docs/ROADMAP.md`, `README.md`

---

### Task 1: Stats con posición + resumen por posición

**Files:** Modify `src/stats.h`, `src/stats.c`, `tests/test_stats.c`

- [ ] **Step 1: Ampliar el test**

En `tests/test_stats.c`, antes del bloque `stats_clear()` añade:

```c
	/* Posiciones: etiquetar puntos y resumir por posición. */
	p.wall_ms = 2000; p.rate = 1.0; p.position = POSITION_DU; stats_add(&p);
	p.wall_ms = 2001; p.rate = 3.0; p.position = POSITION_DU; stats_add(&p);
	p.wall_ms = 2002; p.rate = 5.0; p.position = POSITION_DD; stats_add(&p);
	CHECK(stats_summary_pos(POSITION_DU, 0, &s) == 2, "pos count");
	CHECK(fabs(s.mean - 2.0) < 1e-9, "pos mean");
	CHECK(fabs(s.min - 1.0) < 1e-9 && fabs(s.max - 3.0) < 1e-9, "pos min/max");
	CHECK(stats_summary_pos(POSITION_DD, 0, &s) == 1, "dd count");
	CHECK(fabs(s.max - 5.0) < 1e-9, "dd max");
	CHECK(stats_summary_pos(POSITION_CR, 0, &s) == 0, "cr count");

	/* mean_be / mean_amp en el resumen global */
	CHECK(stats_summary(0, &s) > 0, "summary ok");
	(void)s.mean_be; (void)s.mean_amp;
```

- [ ] **Step 2: Ejecutar para verlo fallar**

```bash
make check 2>&1 | tail -6
```

Expected: falla (POSITION_DU, stats_summary_pos y los campos nuevos no existen).

- [ ] **Step 3: Ampliar `src/stats.h`**

Añade al inicio del header:

```c
#define POSITION_NONE 0
#define POSITION_DU   1   /* dial up */
#define POSITION_DD   2   /* dial down */
#define POSITION_CU   3   /* crown up */
#define POSITION_CD   4   /* crown down */
#define POSITION_CL   5   /* crown left */
#define POSITION_CR   6   /* crown right */

/* Nombre legible de la posición (o "none"). */
const char *position_name(int pos);
```

En `struct stats_point`, añade `int position;` (tras `amp`). En `struct stats_summary`, añade `double mean_be, mean_amp;` (tras `max`). Prototipo nuevo:

```c
/* Resumen del rate por posición (pos == POSITION_NONE -> todas). */
int stats_summary_pos(int pos, uint64_t window_ms, struct stats_summary *out);
```

- [ ] **Step 4: Implementar en `src/stats.c`**

Añade:

```c
const char *position_name(int pos)
{
	static const char *names[POSITION_CR + 1] = {
		"none", "dial up", "dial down", "crown up", "crown down",
		"crown left", "crown right"
	};
	return pos >= POSITION_NONE && pos <= POSITION_CR ? names[pos] : "?";
}
```

Refactoriza `stats_summary` para aceptar un filtro de posición (factor común `stats_summary_filter(pos, window_ms, out)`) y haz que ambos la llamen:

```c
static int stats_summary_filter(int pos, uint64_t window_ms, struct stats_summary *out)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	uint64_t newest = st.n ? st.pts[(start + st.n - 1) % STATS_CAP].wall_ms : 0;
	double sum = 0, sq = 0, mn = 0, mx = 0, sb = 0, sa = 0;
	int cnt = 0, i;
	for(i = 0; i < st.n; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(pos != POSITION_NONE && p->position != pos) continue;
		if(window_ms && p->wall_ms + window_ms < newest) continue;
		if(!cnt || p->rate < mn) mn = p->rate;
		if(!cnt || p->rate > mx) mx = p->rate;
		sum += p->rate;
		sq += p->rate * p->rate;
		sb += p->be;
		sa += p->amp;
		cnt++;
	}
	if(out) {
		out->n = cnt;
		if(cnt) {
			out->mean = sum / cnt;
			out->sigma = cnt > 1 ? sqrt((sq - cnt * out->mean * out->mean) / (cnt - 1)) : 0;
			out->min = mn;
			out->max = mx;
			out->mean_be = sb / cnt;
			out->mean_amp = sa / cnt;
		} else
			out->mean = out->sigma = out->min = out->max =
				out->mean_be = out->mean_amp = 0;
	}
	pthread_mutex_unlock(&st.m);
	return cnt;
}

int stats_summary(uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(POSITION_NONE, window_ms, out);
}

int stats_summary_pos(int pos, uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(pos, window_ms, out);
}
```

(Reemplaza la implementación actual de `stats_summary`; el resto del módulo no cambia.)

- [ ] **Step 5: Verificar y commit**

```bash
make check 2>&1 | tail -6
git add src/stats.h src/stats.c tests/test_stats.c
git commit -m "Add watch-position tagging and per-position stats summary"
```

---

### Task 2: Módulo `report` (CSV + PDF con Cairo) + tests

**Files:** Create `src/report.h`, `src/report.c`, `tests/test_report.c`; Modify `Makefile.am`, `.gitignore`

- [ ] **Step 1: Escribir el test (falla por falta del módulo)**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stats.h"
#include "report.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	stats_init();
	struct stats_point p = { 0, 0, 0, 0, 0 };
	int i;
	for(i = 0; i < 10; i++) {
		p.wall_ms = 1000 + i;
		p.rate = i;
		p.be = 1.0;
		p.amp = 180.0;
		p.position = POSITION_DU;
		stats_add(&p);
	}
	p.wall_ms = 2000; p.rate = 50.0; p.be = 2.0; p.amp = 190.0;
	p.position = POSITION_DD;
	stats_add(&p);

	struct report_row rows[7];
	int n = report_summary(rows, 7);
	CHECK(n == 2, "two positions");
	int du = -1, dd = -1;
	for(i = 0; i < n; i++) {
		if(rows[i].position == POSITION_DU) du = i;
		if(rows[i].position == POSITION_DD) dd = i;
	}
	CHECK(du >= 0 && dd >= 0, "rows found");
	CHECK(rows[du].n == 10, "du count");
	CHECK(fabs(rows[du].mean - 4.5) < 1e-9, "du mean");
	CHECK(fabs(rows[du].mean_be - 1.0) < 1e-9, "du mean_be");
	CHECK(fabs(rows[du].mean_amp - 180.0) < 1e-9, "du mean_amp");
	CHECK(rows[dd].n == 1 && fabs(rows[dd].max - 50.0) < 1e-9, "dd row");

	CHECK(report_write_csv("test_report_out/report.csv", rows, n) == 0, "csv ok");
	{
		char buf[4096] = {0};
		FILE *f = fopen("test_report_out/report.csv", "r");
		CHECK(f != NULL, "csv exists");
		if(f) {
			size_t r = fread(buf, 1, sizeof(buf) - 1, f);
			buf[r] = 0;
			fclose(f);
			CHECK(strstr(buf, "position,n,mean,sigma,min,max,mean_be,mean_amp") != NULL, "csv header");
			CHECK(strstr(buf, "dial up") != NULL, "csv du row");
			CHECK(strstr(buf, "dial down") != NULL, "csv dd row");
		}
	}

	CHECK(report_write_pdf("test_report_out/report.pdf", rows, n) == 0, "pdf ok");
	{
		FILE *f = fopen("test_report_out/report.pdf", "r");
		CHECK(f != NULL, "pdf exists");
		if(f) {
			char hdr[5] = {0};
			fread(hdr, 1, 4, f);
			fclose(f);
			CHECK(!strcmp(hdr, "%PDF"), "pdf header");
		}
	}

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("report tests passed\n");
	return 0;
}
```

(Nota: requiere `mkdir("test_report_out", 0755);` + `rmdir` al final, y `#include <sys/stat.h>`.)

- [ ] **Step 2: Ejecutar para verlo fallar**

```bash
make check 2>&1 | tail -6
```

- [ ] **Step 3: Implementar `src/report.h`** (cabecera GPL-2):

```c
#ifndef TG_REPORT_H
#define TG_REPORT_H

struct report_row {
	int position;
	int n;
	double mean, sigma, min, max;
	double mean_be, mean_amp;
};

/* Resumen por posición desde el anillo de stats (excluye POSITION_NONE).
 * Devuelve el nº de filas escritas (<= max_rows). */
int report_summary(struct report_row *rows, int max_rows);

/* Escribe el informe CSV en `path`. Devuelve 0 = ok. */
int report_write_csv(const char *path, const struct report_row *rows, int n);

/* Escribe el informe PDF (A4, Cairo) en `path`. Devuelve 0 = ok. */
int report_write_pdf(const char *path, const struct report_row *rows, int n);

#endif
```

- [ ] **Step 4: Implementar `src/report.c`** (cabecera GPL-2):

```c
#include "report.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cairo.h>
#include <cairo-pdf.h>

int report_summary(struct report_row *rows, int max_rows)
{
	int n = 0;
	int pos;
	for(pos = 1; pos <= POSITION_CR && n < max_rows; pos++) {
		struct stats_summary s;
		if(stats_summary_pos(pos, 0, &s) <= 0) continue;
		rows[n].position = pos;
		rows[n].n = s.n;
		rows[n].mean = s.mean;
		rows[n].sigma = s.sigma;
		rows[n].min = s.min;
		rows[n].max = s.max;
		rows[n].mean_be = s.mean_be;
		rows[n].mean_amp = s.mean_amp;
		n++;
	}
	return n;
}

int report_write_csv(const char *path, const struct report_row *rows, int n)
{
	FILE *f = fopen(path, "w");
	if(!f) return 1;
	fprintf(f, "position,n,mean,sigma,min,max,mean_be,mean_amp\n");
	int i;
	for(i = 0; i < n; i++) {
		fprintf(f, "%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
			position_name(rows[i].position), rows[i].n,
			rows[i].mean, rows[i].sigma, rows[i].min, rows[i].max,
			rows[i].mean_be, rows[i].mean_amp);
	}
	return fclose(f) ? 1 : 0;
}

/* Tabla de texto en PDF: columnas fijas x, ancho A4 595 pts. */
static void pdf_text(cairo_t *cr, double x, double y, const char *s)
{
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
}

int report_write_pdf(const char *path, const struct report_row *rows, int n)
{
	cairo_surface_t *sfc = cairo_pdf_surface_create(path, 595, 842);
	if(cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(sfc);
		return 1;
	}
	cairo_t *cr = cairo_create(sfc);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 18);
	pdf_text(cr, 40, 60, "Tg timing report");
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 10);

	double cols[] = { 40, 130, 170, 220, 270, 320, 380, 430 };
	const char *hdr[] = { "Position", "n", "Mean", "Sigma", "Min", "Max", "BE ms", "Amp deg" };
	double y = 90;
	int c;
	for(c = 0; c < 8; c++)
		pdf_text(cr, cols[c], y, hdr[c]);
	y += 16;
	int i;
	for(i = 0; i < n; i++) {
		char buf[64];
		pdf_text(cr, cols[0], y, position_name(rows[i].position));
		snprintf(buf, sizeof(buf), "%d", rows[i].n);
		pdf_text(cr, cols[1], y, buf);
		snprintf(buf, sizeof(buf), "%.3f", rows[i].mean);
		pdf_text(cr, cols[2], y, buf);
		snprintf(buf, sizeof(buf), "%.3f", rows[i].sigma);
		pdf_text(cr, cols[3], y, buf);
		snprintf(buf, sizeof(buf), "%.3f", rows[i].min);
		pdf_text(cr, cols[4], y, buf);
		snprintf(buf, sizeof(buf), "%.3f", rows[i].max);
		pdf_text(cr, cols[5], y, buf);
		snprintf(buf, sizeof(buf), "%.2f", rows[i].mean_be);
		pdf_text(cr, cols[6], y, buf);
		snprintf(buf, sizeof(buf), "%.1f", rows[i].mean_amp);
		pdf_text(cr, cols[7], y, buf);
		y += 16;
	}
	pdf_text(cr, 40, 800, "Generated by tg (https://github.com/vacaboja/tg)");

	cairo_show_page(cr);
	cairo_destroy(cr);
	cairo_surface_finish(sfc);
	cairo_surface_destroy(sfc);
	return 0;
}
```

- [ ] **Step 5: Registrar en `Makefile.am` y `.gitignore`**

Añade `src/report.c` a `tg_timer_SOURCES`. Tests:

```make
check_PROGRAMS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats tests/test_report
tests_test_report_SOURCES = tests/test_report.c src/report.c src/stats.c
tests_test_report_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_report_LDADD = $(GTK_LIBS) -lpthread -lm
TESTS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats tests/test_report
```

`.gitignore`: añade `/tests/test_report`, `/test_report_out/`.

```bash
autoreconf -i 2>&1 | tail -2 && ./configure 2>&1 | tail -2
make check 2>&1 | tail -12
```

Expected: 5 tests PASS. (El PDF se genera headless con cairo — no necesita display.)

- [ ] **Step 6: Commit**

```bash
git add src/report.h src/report.c tests/test_report.c Makefile.am .gitignore
git commit -m "Add report module (CSV + Cairo PDF) with tests"
```

---

### Task 3: UI — combo de posición + "Export report..."

**Files:** Modify `src/interface.c`, `src/tg.h`

- [ ] **Step 1: Campo en `struct main_window` (`src/tg.h`)**

Junto a `level_bar`:

```c
	GtkWidget *position_combo;
	int position;   /* POSITION_* actual (etiqueta de los ciclos en vivo) */
```

- [ ] **Step 2: Combo en `init_main_window` (tras el medidor de nivel)**

```c
	// Position selector
	label = gtk_label_new("pos");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	w->position_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "none");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "dial up");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "dial down");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "crown up");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "crown down");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "crown left");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->position_combo), "crown right");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w->position_combo), 0);
	gtk_box_pack_start(GTK_BOX(hbox), w->position_combo, FALSE, FALSE, 0);
	g_signal_connect(w->position_combo, "changed", G_CALLBACK(handle_position_change), w);
```

- [ ] **Step 3: Handler (tras `handle_gain_change`)**

```c
static void handle_position_change(GtkComboBox *b, struct main_window *w)
{
	if(!w->controls_active) return;
	w->position = gtk_combo_box_get_active(b);
}
```

En `start_interface`, tras `memset(w, 0, ...)`, inicializa `w->position = POSITION_NONE;` (requiere `#include "stats.h"`, ya presente).

- [ ] **Step 4: Etiquetar los ciclos en `refresh()`**

En el bloque de `stats_add` (tras `sp.amp = sc.amp;`) añade:

```c
	sp.position = w->position;
```

- [ ] **Step 5: Menú "Export report..." + handler**

Handler (tras `handle_save_session_log`):

```c
static void handle_export_report(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	char *dir = g_build_filename(g_get_home_dir(), "tg-logs", NULL);
	if(g_mkdir_with_parents(dir, 0755)) {
		error("Cannot create report directory %s", dir);
		g_free(dir);
		return;
	}
	char base[64];
	time_t t = time(NULL);
	strftime(base, sizeof(base), "tg-report-%Y%m%d-%H%M%S", localtime(&t));
	struct report_row rows[POSITION_CR];
	int n = report_summary(rows, POSITION_CR);
	int err = 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.csv", dir, base);
	if(report_write_csv(path, rows, n)) err++;
	snprintf(path, sizeof(path), "%s/%s.pdf", dir, base);
	if(report_write_pdf(path, rows, n)) err++;
	if(err)
		error("Report: %d file(s) failed in %s", err, dir);
	else {
		GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->window), 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			"Report saved to\n%s/%s.{csv,pdf}", dir, base);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	g_free(dir);
}
```

(Requiere `#include "report.h"`.) Ítem de menú, tras "Save session log":

```c
	// ... Export report
	GtkWidget *report_item = gtk_menu_item_new_with_label("Export report...");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), report_item);
	g_signal_connect(report_item, "activate", G_CALLBACK(handle_export_report), w);
```

- [ ] **Step 6: Compilar y verificar**

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make check 2>&1 | tail -6
make test 2>&1 | tail -3
```

- [ ] **Step 7: Commit**

```bash
git add src/interface.c src/tg.h
git commit -m "Add position selector and report export menu"
```

---

### Task 4: Verificación, docs y cierre

**Files:** Modify `docs/ROADMAP.md`, `README.md`

- [ ] **Step 1: Verificación funcional**

```bash
make check 2>&1 | tail -6
timeout 8 ./tg-timer >/dev/null 2>&1
```

Expected: 5 tests PASS; la app abre con el combo "pos" y el ítem "Export report..." en el menú.

- [ ] **Step 2: `docs/ROADMAP.md` — Fase 5 completada**

```markdown
## Fase 5 — Multi-posición + informe (completada)

**Rama:** `feature/positions-report` — **Estado:** completada.

- Selector de posición en la UI (none / dial up / dial down / crown up /
  crown down / crown left / crown right) que etiqueta los ciclos en vivo.
- Resumen por posición (n, media/σ/min/max del rate + media de be/amp).
- Menú Command → *Export report...*: escribe en `~/tg-logs/`
  `tg-report-<timestamp>.{csv,pdf}` (PDF generado con Cairo).
```

- [ ] **Step 3: `README.md`**

```markdown
## Positions and report

Select the watch position (dial up/down, crown up/down/left/right) with the
"pos" selector to tag the live measurements. Command menu → *Export report...*
writes a per-position summary to `~/tg-logs/tg-report-<timestamp>.{csv,pdf}`.
```

- [ ] **Step 4: Verificación completa + commit + push + merge**

```bash
make check 2>&1 | tail -6
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
git add -A
git commit -m "Document positions and report export"
git push origin feature/positions-report
git checkout master && git merge feature/positions-report && git push origin master
git branch -d feature/positions-report
git push origin --delete feature/positions-report
```

---

## Self-review

- **Cobertura:** stats con posición → Task 1; módulo report (CSV/PDF) → Task 2; UI (combo + menú) → Task 3; docs/merge → Task 4.
- **Enlace headless:** `report.c` usa solo stats.h + cairo (cabeceras vía GTK_CFLAGS; link con `$(GTK_LIBS)` en el test). El PDF se genera sin display.
- **Sin placeholders:** todo el código incluido.
- **Consistencia:** `POSITION_*`, `position_name`, `stats_summary_pos`, `report_summary/write_csv/write_pdf`, `handle_position_change`, `handle_export_report` usan los mismos nombres en todas las tasks. `w->position` se inicializa a `POSITION_NONE`.