# Watch-panel (5.5b) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Panel izquierdo (cuaderno del relojero) que consume `watchdb.c`: registrar relojes, medir sesiones etiquetadas con posición y configuración, y consultar el historial por reloj.

**Architecture:** Nuevo módulo `src/watch_panel.c/.h` (UI GTK, usa `watchdb.h`). `init_main_window` reestructura la ventana en un `GtkPaned`: izquierda el panel de relojes/sesiones, derecha el contenido actual (controles + notebook). `watchdb_open("~/tg-data/tg.db")` en `start_interface` (crea el directorio si falta) y `watchdb_close()` en shutdown. Ciclo de sesión manual: *Iniciar sesión* toma el reloj seleccionado y guarda `start_ms`; *Finalizar y guardar* llama `watchdb_capture_session()` con la posición del combo "pos", la nota del entry y la config actual. El historial se muestra en un `GtkTreeView` (fecha, posición, n, media, σ, be, amp).

**Tech Stack:** C99, GTK3 (GtkPaned, GtkListBox, GtkTreeView), SQLite (vía watchdb).

**Rama:** `feature/watch-panel` (creada desde `master`).

---

## Estructura de archivos

- Create: `src/watch_panel.h`, `src/watch_panel.c`
- Modify: `src/interface.c`, `src/tg.h`, `Makefile.am`
- Docs: `docs/ROADMAP.md`, `README.md`, `docs/USAGE.md`

---

### Task 1: Ciclo de vida de la base

**Files:** Modify `src/interface.c`

- [ ] En `start_interface`, tras `stats_init();` añadir:

```c
	{
		char *dir = g_build_filename(g_get_home_dir(), "tg-data", NULL);
		g_mkdir_with_parents(dir, 0755);
		char *db = g_build_filename(dir, "tg.db", NULL);
		if(watchdb_open(db))
			error("Cannot open watch database %s", db);
		g_free(db);
		g_free(dir);
	}
```

- [ ] En `on_shutdown`, antes de `close_config(w);` añadir `watchdb_close();`.

- [ ] Verificar: build + `ls ~/tg-data/` muestra `tg.db` tras ejecutar la app.

### Task 2: Panel izquierdo + CRUD de relojes

**Files:** Create `src/watch_panel.h`, `src/watch_panel.c`; Modify `src/tg.h`, `src/interface.c`, `Makefile.am`

- [ ] `src/tg.h` — campos en `struct main_window` (junto a `position_combo`):

```c
	GtkWidget *watch_panel;          /* GtkPaned izquierdo */
	GtkWidget *watch_list;           /* GtkListBox de relojes */
	GtkWidget *session_tree;         /* GtkTreeView de sesiones */
	GtkWidget *session_note_entry;
	GtkWidget *session_start_button;
	GtkWidget *session_finish_button;
	GtkWidget *watch_delete_button;
	int64_t selected_watch_id;       /* -1 = ninguno */
	char selected_watch_name[64];
	int session_active;
	uint64_t session_start_ms;
```

- [ ] `src/watch_panel.h` (GPL-2):

```c
GtkWidget *watch_panel_build(struct main_window *w);
void watch_panel_refresh(struct main_window *w);
```

- [ ] `src/watch_panel.c` — UI del panel (detalles en el brief del implementador):
  - `watch_panel_build(w)`: columna vertical — encabezado "Watches", GtkListBox de relojes, botones "New watch" / "Delete" (Delete con confirmación), entrada de nota, botones "Start session" / "Finish & save" (mutuamente sensibles), GtkTreeView de sesiones (columnas: Date, Pos, n, Mean s/d, σ, BE ms, Amp deg).
  - Selección de reloj → carga sesiones (`watchdb_load_sessions(id)`) y refresca el árbol.
  - "New watch..." → diálogo (nombre obligatorio, marca, modelo) → `watchdb_add_watch` → refrescar y seleccionar.
  - "Delete" → confirmación → `watchdb_remove_watch(idx)` → refrescar.
  - `watch_panel_refresh(w)`: reconstruye la lista de relojes y la de sesiones (usado tras crear/borrar/seleccionar).
- [ ] `src/interface.c` `init_main_window`: envolver `vbox` (controles + notebook) en un `GtkPaned` con el panel a la izquierda:

```c
	GtkWidget *right = vbox;   /* el vbox actual con hbox + notebook */
	GtkWidget *left = watch_panel_build(w);
	w->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(w->paned), left, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(w->paned), right, TRUE, FALSE);
	gtk_paned_set_position(GTK_PANED(w->paned), 280);
	gtk_container_add(GTK_CONTAINER(w->window), w->paned);
```

(sustituyendo el `gtk_container_add(GTK_CONTAINER(w->window), vbox);` actual)

- [ ] `Makefile.am`: añadir `src/watch_panel.c` a `tg_timer_SOURCES`.

### Task 3: Ciclo de sesión

- [ ] "Start session": exige reloj seleccionado y `!session_active`; guarda `selected_watch_id` y `session_start_ms = g_get_real_time()/1000`; `session_active = 1`; actualiza sensibilidad de botones y el label de estado.
- [ ] "Finish & save": llama

```c
	watchdb_capture_session(w->selected_watch_id, w->session_start_ms,
	                        g_get_real_time()/1000, w->position,
	                        nota_del_entry, w->bph, w->la, w->cal, w->gain,
	                        w->filter_cutoff);
```

  refresca el árbol de sesiones y `session_active = 0`.
- [ ] El estado "sesión en curso" se muestra en el panel (p. ej. el botón Finish sensible y un label con la duración en curso).

### Task 4: Verificación + docs + merge

- [ ] `make check` 7/7, build sin warnings, smoke.
- [ ] Verificación funcional: crear reloj → iniciar sesión → medir → finalizar → la sesión aparece en el historial y en `~/tg-data/tg.db` (verificable con `sqlite3`).
- [ ] Docs: ROADMAP 5.5b completada, README (feature + captura del panel), USAGE (flujo del cuaderno del relojero). Merge a master + push + borrar rama.

---

## Fuera de alcance (iteración siguiente)

- Export del historial por reloj (CSV/PDF) y gráfico de evolución por sesión.
- Edición de defaults por reloj (bph/lift_angle) desde la UI (la API ya lo soporta vía `watchdb_rename_watch`).