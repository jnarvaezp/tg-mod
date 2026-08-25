# Informe vacío: fila "none" + Total — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Que el informe exportado nunca salga vacío: incluir los ciclos sin posición etiquetada (fila "none"), añadir una fila "Total" (todas las posiciones), y escribir "no data" si el anillo de stats está vacío.

**Architecture:** `report_summary` hoy itera posiciones 1–6 y descarta los ciclos con `position == POSITION_NONE` (0), por eso el export salía con solo la cabecera. Se añade `stats_summary_untagged()` en stats (filtra `position == POSITION_NONE`; `stats_summary_pos(POSITION_NONE)` significa "todas" y no sirve para esto). `report_summary` genera: fila "none" (si hay ciclos sin etiquetar) → posiciones 1–6 → fila "Total" (`position = -1`) si hay datos. Los escritores CSV/PDF formatean `position < 0` como "total" y emiten "no data" si `n == 0`. Se define `REPORT_MAX_ROWS = POSITION_CR + 2` (8) y se corrige el llamador en `interface.c` (hoy asigna `rows[POSITION_CR]` = 6, insuficiente con las 2 filas nuevas → riesgo de overflow).

**Tech Stack:** C99, pthread, autotools `make check`.

**Rama:** `feature/report-fix` (creada desde `master`).

---

## Estructura de archivos

- Modify: `src/stats.h`, `src/stats.c`, `tests/test_stats.c`, `src/report.h`, `src/report.c`, `tests/test_report.c`, `src/interface.c`
- Docs: `docs/ROADMAP.md` (marcar el fix como hecho)

---

### Task 1: `stats_summary_untagged` + tests

**Files:** Modify `src/stats.h`, `src/stats.c`, `tests/test_stats.c`

- [ ] **Step 1: Ampliar el test** (en `tests/test_stats.c`, dentro del bloque de posiciones existente, tras el check `cr count`):

```c
	/* Ciclos sin etiquetar: resumen separado. Nota: los 5 puntos iniciales
	 * del test tienen position == POSITION_NONE (struct inicializado a 0),
	 * asi que el resumen untagged incluye todo; usa ventana de 1 ms para
	 * aislar el punto nuevo (wall_ms=3000). */
	p.wall_ms = 3000; p.rate = 7.0; p.position = POSITION_NONE; stats_add(&p);
	CHECK(stats_summary_untagged(1, &s) == 1, "untagged count");
	CHECK(fabs(s.mean - 7.0) < 1e-9, "untagged mean");
	CHECK(stats_summary(0, &s) > 0, "all still works");
```

- [ ] **Step 2: Ejecutar para verlo fallar**

```bash
make check 2>&1 | tail -6
```

Expected: falla (función inexistente).

- [ ] **Step 3: Implementar**

En `src/stats.h`, tras `stats_summary_pos`:

```c
/* Resumen del rate de los ciclos SIN posición etiquetada. */
int stats_summary_untagged(uint64_t window_ms, struct stats_summary *out);
```

En `src/stats.c`, refactoriza `stats_summary_filter` para aceptar un flag
`only_untagged` (filtra `p->position == POSITION_NONE` cuando es 1; con 0
mantiene el comportamiento actual de posicion/ventana) y añade el wrapper:

```c
static int stats_summary_filter(int pos, uint64_t window_ms,
                                int only_untagged, struct stats_summary *out)
{
	pthread_mutex_lock(&st.m);
	int start = st.n < STATS_CAP ? 0 : st.wp;
	uint64_t newest = st.n ? st.pts[(start + st.n - 1) % STATS_CAP].wall_ms : 0;
	double sum = 0, sq = 0, mn = 0, mx = 0, sb = 0, sa = 0;
	int cnt = 0, i;
	for(i = 0; i < st.n; i++) {
		const struct stats_point *p = &st.pts[(start + i) % STATS_CAP];
		if(only_untagged) {
			if(p->position != POSITION_NONE) continue;
		} else if(pos != POSITION_NONE && p->position != pos)
			continue;
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
	return stats_summary_filter(POSITION_NONE, window_ms, 0, out);
}

int stats_summary_pos(int pos, uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(pos, window_ms, 0, out);
}

int stats_summary_untagged(uint64_t window_ms, struct stats_summary *out)
{
	return stats_summary_filter(POSITION_NONE, window_ms, 1, out);
}
```

- [ ] **Step 4: Verificar y commit**

```bash
make check 2>&1 | tail -6
git add src/stats.h src/stats.c tests/test_stats.c
git commit -m "Add stats summary for untagged cycles"
```

---

### Task 2: `report_summary` con "none" + "Total" + "no data"

**Files:** Modify `src/report.h`, `src/report.c`, `tests/test_report.c`, `src/interface.c`

- [ ] **Step 1: `REPORT_MAX_ROWS` en `src/report.h`**

```c
#define REPORT_MAX_ROWS (POSITION_CR + 2)   /* none + 6 posiciones + total */
```

(Requiere que `report.h` incluya `stats.h` para `POSITION_CR`.)

- [ ] **Step 2: `report_summary` en `src/report.c`**

```c
int report_summary(struct report_row *rows, int max_rows)
{
	int n = 0;
	struct stats_summary s;
	/* Ciclos sin posición etiquetada. */
	if(stats_summary_untagged(0, &s) > 0 && n < max_rows) {
		rows[n].position = POSITION_NONE;
		rows[n].n = s.n;
		rows[n].mean = s.mean;
		rows[n].sigma = s.sigma;
		rows[n].min = s.min;
		rows[n].max = s.max;
		rows[n].mean_be = s.mean_be;
		rows[n].mean_amp = s.mean_amp;
		n++;
	}
	/* Posiciones etiquetadas. */
	int pos;
	for(pos = 1; pos <= POSITION_CR && n < max_rows; pos++) {
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
	/* Total (todas las posiciones). */
	if(n > 0 && n < max_rows && stats_summary(0, &s) > 0) {
		rows[n].position = -1;
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
```

- [ ] **Step 3: Nombre de fila en los escritores**

En `report_write_csv` y `report_write_pdf`, sustituye `position_name(rows[i].position)` por un helper:

```c
static const char *row_name(int position)
{
	return position < 0 ? "total" : position_name(position);
}
```

- [ ] **Step 4: "no data" si `n == 0`**

En `report_write_csv`, tras la cabecera:

```c
	if(n == 0) {
		fprintf(f, "no data\n");
		return fclose(f) ? 1 : 0;
	}
```

En `report_write_pdf`, tras crear el cairo y antes de la tabla (o al final si no hay filas):

```c
	if(n == 0) {
		cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 12);
		pdf_text(cr, 40, 120, "No measurements");
	}
```

(El resto del PDF sigue igual; con n==0 el bucle de filas no dibuja nada.)

- [ ] **Step 5: Tests en `tests/test_report.c`**

Añade un punto sin etiquetar y ajusta las expectativas:

```c
	p.wall_ms = 3000; p.rate = 9.0; p.be = 3.0; p.amp = 160.0;
	p.position = POSITION_NONE;
	stats_add(&p);
```

Cambia las aserciones:

```c
	struct report_row rows[REPORT_MAX_ROWS];
	int n = report_summary(rows, REPORT_MAX_ROWS);
	CHECK(n == 4, "four rows");   /* none + du + dd + total */
	int none = -1, du = -1, dd = -1, tot = -1;
	for(i = 0; i < n; i++) {
		if(rows[i].position == POSITION_NONE) none = i;
		if(rows[i].position == POSITION_DU) du = i;
		if(rows[i].position == POSITION_DD) dd = i;
		if(rows[i].position < 0) tot = i;
	}
	CHECK(none >= 0 && du >= 0 && dd >= 0 && tot >= 0, "rows found");
	CHECK(rows[none].n == 1 && fabs(rows[none].mean - 9.0) < 1e-9, "none row");
	CHECK(rows[du].n == 10, "du count");
	CHECK(fabs(rows[du].mean - 4.5) < 1e-9, "du mean");
	CHECK(fabs(rows[du].mean_be - 1.0) < 1e-9, "du mean_be");
	CHECK(fabs(rows[du].mean_amp - 180.0) < 1e-9, "du mean_amp");
	CHECK(rows[dd].n == 1 && fabs(rows[dd].max - 50.0) < 1e-9, "dd row");
	CHECK(rows[tot].n == 12, "total count");
	CHECK(fabs(rows[tot].mean - (0+1+2+3+4+5+6+7+8+9+50+9)/12.0) < 1e-9, "total mean");
```

Y en el CSV check, añade:

```c
			CHECK(strstr(buf, "none,1,") != NULL, "csv none row");
			CHECK(strstr(buf, "total,12,") != NULL, "csv total row");
```

(El check del header y de "dial up"/"dial down" se mantienen.)

- [ ] **Step 6: Llamador en `src/interface.c`**

En `handle_export_report`, cambia:

```c
	struct report_row rows[POSITION_CR];
	int n = report_summary(rows, POSITION_CR);
```
por:
```c
	struct report_row rows[REPORT_MAX_ROWS];
	int n = report_summary(rows, REPORT_MAX_ROWS);
```

- [ ] **Step 7: Verificar y commit**

```bash
make check 2>&1 | tail -8
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build ok"
make test 2>&1 | tail -3
git add src/report.h src/report.c src/interface.c tests/test_report.c
git commit -m "Include untagged and total rows in report export"
```

---

### Task 3: Verificación funcional + docs + merge

**Files:** Modify `docs/ROADMAP.md`

- [ ] **Step 1: Verificación funcional**

```bash
make check 2>&1 | tail -6
timeout 8 ./tg-timer >/dev/null 2>&1
```

Expected: 5 tests PASS; la app abre sin warnings. (El informe con datos sin etiquetar ahora mostrará la fila "none" + "total".)

- [ ] **Step 2: `docs/ROADMAP.md` — marcar el fix como hecho**

```markdown
## Fix pendiente — Informe vacío (hecho)

**Rama:** `feature/report-fix` — **Estado:** completada.

El informe exportado excluía los ciclos sin posición etiquetada. Ahora
incluye la fila "none" (ciclos sin etiquetar) y la fila "Total", y escribe
"no data" si no hay mediciones.
```

- [ ] **Step 3: Verificación completa + commit + push + merge**

```bash
make check 2>&1 | tail -6
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
git add -A
git commit -m "Document report fix"
git push origin feature/report-fix
git checkout master && git merge feature/report-fix && git push origin master
git branch -d feature/report-fix
git push origin --delete feature/report-fix
```

---

## Self-review

- **Cobertura:** untagged → Task 1; none+total+no-data+buffer → Task 2; verificación/docs/merge → Task 3.
- **Overflow:** el llamador pasa de `rows[POSITION_CR]` (6) a `rows[REPORT_MAX_ROWS]` (8); `report_summary` respeta `max_rows` en cada append.
- **Sin placeholders:** todo el código incluido.
- **Consistencia:** `stats_summary_untagged`, `REPORT_MAX_ROWS`, `row_name`, `position = -1` (total) usados con el mismo nombre en todas las tasks.